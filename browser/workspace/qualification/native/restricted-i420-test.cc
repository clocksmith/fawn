// Standalone CPU regression compiled against the pinned Chromium component build.
#include <array>
#include <cstdio>
#include <vector>
#include "base/at_exit.h"
#include "base/test/test_discardable_memory_allocator.h"
#include "base/task/single_thread_task_executor.h"
#include "cc/paint/skia_paint_canvas.h"
#include "media/gfx/paint_canvas_video_renderer.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColorSpace.h"

#include "base/check.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "third_party/blink/renderer/modules/webcodecs/fawn_restricted_plane_cache.h"

// Same CPU Skia paint, blend mode, destination color space and filter used by
// VideoFrame::ConvertAndCopyToRGB for a directly readable full-size capture.
std::vector<uint8_t> Srgb(const scoped_refptr<media::VideoFrame>& frame) {
  auto size = frame->natural_size();
  std::vector<uint8_t> pixels(4u * size.width() * size.height());
  auto info = SkImageInfo::Make(size.width(), size.height(), kRGBA_8888_SkColorType,
                                kUnpremul_SkAlphaType, SkColorSpace::MakeSRGB());
  auto target = SkCanvas::MakeRasterDirect(info, pixels.data(), size.width() * 4);
  CHECK(target);
  cc::SkiaPaintCanvas canvas(target.get());
  cc::PaintFlags flags;
  flags.setBlendMode(SkBlendMode::kSrc);
  flags.setFilterQuality(cc::PaintFlags::FilterQuality::kNone);
  media::PaintCanvasVideoRenderer renderer;
  media::PaintCanvasVideoRenderer::PaintParams params;
  params.dest_rect = gfx::RectF(size);
  renderer.Paint(frame, &canvas, flags, params, nullptr);
  return pixels;
}

void CheckSrgb(const scoped_refptr<media::VideoFrame>& frame, uint8_t blue) {
  auto pixels = Srgb(frame);
  const std::array<uint8_t,4> expected{114,5,blue,255};
  for (size_t i=0; i<pixels.size(); ++i)
    CHECK(pixels[i] == expected[i%4]);
}

// Exercise exactly the native VideoFrame crop and paint used by copyTo(RGBA).
// Reconstruct changed frame from its old full image plus the reported tile reads.
void CheckPartial(const scoped_refptr<media::VideoFrame>& frame,
                  const std::vector<uint8_t>& previous,
                  base::span<const uint32_t> tiles) {
  const auto size = frame->coded_size();
  const int w = size.width(), h = size.height(), cols = (w + 63) / 64;
  const auto expected = Srgb(frame);
  auto actual = previous;
  CHECK_EQ(actual.size(), expected.size());
  for (uint32_t tile : tiles) {
    const int x = (tile % cols) * 64, y = (tile / cols) * 64;
    const gfx::Rect rect(x, y, std::min(64, w - x), std::min(64, h - y));
    auto cropped = media::VideoFrame::WrapVideoFrame(frame, frame->format(), rect, rect.size());
    CHECK(cropped);
    const auto bytes = Srgb(cropped);
    for (int r = 0; r < rect.height(); ++r)
      std::copy_n(bytes.begin() + r * rect.width() * 4, rect.width() * 4,
                  actual.begin() + ((r + y) * w + x) * 4);
  }
  if (actual != expected) {
    const auto pos = std::mismatch(actual.begin(), actual.end(), expected.begin()).first - actual.begin();
    std::fprintf(stderr, "cropped canonical mismatch byte %td at (%td,%td), expected %u actual %u\n",
                 pos, (pos/4)%w, (pos/4)/w, expected[pos], actual[pos]);
  }
  CHECK(actual == expected);
}

void CheckTileChanges(int w, int h) {
  const size_t luma = static_cast<size_t>(w) * h;
  const size_t chroma = luma / 4;
  auto region = base::ReadOnlySharedMemoryRegion::Create(luma + 2 * chroma);
  auto bytes = region.mapping.GetMemoryAsSpan<uint8_t>();
  for (size_t i = 0; i < bytes.size(); ++i)
    bytes[i] = static_cast<uint8_t>(i * 19 + i / 7);
  const gfx::Size size(w, h);
  const media::CaptureVersion version(5, 2);
  auto cache = base::MakeRefCounted<blink::FawnRestrictedPlaneCache>(17, version);
  auto other = base::MakeRefCounted<blink::FawnRestrictedPlaneCache>(17, version);
  auto frame = media::VideoFrame::WrapExternalData(media::PIXEL_FORMAT_I420, size,
      gfx::Rect(size), size, bytes, base::TimeDelta());
  CHECK(frame);
  frame->BackWithSharedMemory(&region.region);
  frame->metadata().capture.version = version;
  frame->set_color_space(gfx::ColorSpace::CreateREC709());
  auto first = cache->Compare(17, *frame, nullptr, 0, 64);
  CHECK(first.serial && !first.prior && first.snapshot_bytes_copied == bytes.size());
  CHECK(!cache->Compare(17, *frame, other.get(), first.serial, 64).prior);
  CHECK(!cache->Compare(17, *frame, cache.get(), 999, 64).prior);
  auto previous = Srgb(frame);
  uint32_t serial = first.serial;
  const size_t offsets[] = {size_t(82 * w + 83), luma + (31 * w / 2) + 31,
      luma + chroma + (32 * w / 2) + 32, luma + chroma - 1, luma-1};
  for (size_t offset : offsets) {
    bytes[offset] ^= 127;
    const auto next = cache->Compare(17, *frame, cache.get(), serial, 64);
    CHECK(next.prior == serial && next.serial != serial && next.snapshot_bytes_copied == static_cast<uint32_t>(offset < luma ? w : w/2));
    CHECK(!next.dirty().empty() && next.dirty().size() <= 4);
    CHECK(Srgb(frame) != previous);
    CheckPartial(frame, previous, next.dirty());
    std::printf("shape=%dx%d planeOffset=%zu dirty=%zu snapshotCopy=%u\n",
                w, h, offset, next.dirty().size(), next.snapshot_bytes_copied);
    previous = Srgb(frame);
    serial = next.serial;
  }
  const auto unchanged = cache->Compare(17, *frame, cache.get(), serial, 64);
  CHECK(unchanged.prior == serial && unchanged.dirty().empty() && !unchanged.snapshot_bytes_copied);
  auto cropped = media::VideoFrame::WrapVideoFrame(frame, frame->format(),
      gfx::Rect(2, 0, w-2, h), gfx::Size(w-2, h));
  CHECK(!cache->Compare(17, *cropped, cache.get(), serial, 64).serial);
  auto scaled = media::VideoFrame::WrapVideoFrame(frame, frame->format(), gfx::Rect(size),
      gfx::Size(w/2, h/2));
  CHECK(!cache->Compare(17, *scaled, cache.get(), serial, 64).serial);
  const gfx::Size smaller(w-2, h-2);
  auto resize = media::VideoFrame::WrapExternalData(media::PIXEL_FORMAT_I420, smaller,
      gfx::Rect(smaller), smaller, bytes, base::TimeDelta());
  CHECK(resize);
  resize->BackWithSharedMemory(&region.region);
  resize->set_color_space(frame->ColorSpace());
  resize->metadata().capture.version = version;
  CHECK(!cache->Compare(17, *resize, cache.get(), serial, 64).prior);
  cache->Retire();
  CHECK(!cache->Compare(17, *frame, cache.get(), serial, 64).serial);
}

int main() {
  base::AtExitManager at_exit;
  base::SingleThreadTaskExecutor executor;
  base::TestDiscardableMemoryAllocator allocator;
  base::DiscardableMemoryAllocator::SetInstance(&allocator);
  constexpr int kWidth = 512, kHeight = 256;
  constexpr size_t kLuma = kWidth * kHeight, kChroma = kLuma / 4;
  const media::CaptureVersion version(5, 2);
  auto baseline = base::MakeRefCounted<blink::FawnRestrictedPlaneCache>(17, version);
  auto first_region = base::ReadOnlySharedMemoryRegion::Create(kLuma + 2 * kChroma);
  auto second_region = base::ReadOnlySharedMemoryRegion::Create(kLuma + 2 * kChroma);
  CHECK(first_region.IsValid() && second_region.IsValid());
  auto make_frame = [&](base::MappedReadOnlyRegion& region, uint8_t u) {
    auto bytes = region.mapping.GetMemoryAsSpan<uint8_t>();
    std::ranges::fill(bytes.first(kLuma), 40);
    std::ranges::fill(bytes.subspan(kLuma, kChroma), u);
    std::ranges::fill(bytes.last(kChroma), 176);
    const gfx::Size size(kWidth, kHeight);
    auto frame = media::VideoFrame::WrapExternalData(
        media::PIXEL_FORMAT_I420, size, gfx::Rect(size), size, bytes, base::TimeDelta());
    CHECK(frame);
    frame->BackWithSharedMemory(&region.region);
    frame->metadata().capture.version = version;
    frame->set_color_space(gfx::ColorSpace::CreateREC709());
    return frame;
  };
  auto first = make_frame(first_region, 115);
  auto unchanged = make_frame(second_region, 115);
  CheckSrgb(first, 2);
  CheckSrgb(unchanged, 2);
  CHECK(baseline->Observe(18, *first) == 0);
  CHECK(baseline->Observe(17, *first) == 1);
  CHECK(baseline->Observe(17, *unchanged) == 1); // distinct storage, identical planes
  unchanged.reset();
  auto second = make_frame(second_region, 114);
  CheckSrgb(second, 0);
  CHECK(baseline->Observe(17, *second) == 2); // external texture missed all 32 tiles
  CHECK(baseline->Observe(17, *second) == 2);
  auto collision = base::MakeRefCounted<blink::FawnRestrictedPlaneCache>(17, version);
  const auto collision_initial = collision->Compare(17, *first, nullptr, 0, 64);
  const auto collision_next = collision->Compare(17, *second, collision.get(), collision_initial.serial, 64);
  CHECK_EQ(collision_next.dirty().size(), 32u);
  CheckPartial(second, Srgb(first), collision_next.dirty());
  std::puts("collision: all 32 canonical tiles differ and are freshly read");
  second->metadata().capture.version = media::CaptureVersion(6, 2);
  CHECK(baseline->Observe(17, *second) == 0);
  second->metadata().capture.version = media::CaptureVersion(5, 3);
  CHECK(baseline->Observe(17, *second) == 0);
  second->metadata().capture.version = version;
  second->set_color_space(gfx::ColorSpace::CreateREC601());
  CHECK(baseline->Observe(17, *second) == 3); // same bytes, conversion changed
  auto cropped = media::VideoFrame::WrapVideoFrame(second, media::PIXEL_FORMAT_I420,
      gfx::Rect(2, 2, 100, 100), gfx::Size(100, 100));
  CHECK(cropped && baseline->Observe(17, *cropped) == 0);
  auto unknown = media::VideoFrame::CreateBlackFrame(gfx::Size(32, 32));
  CHECK(unknown);
  unknown->set_color_space(gfx::ColorSpace::CreateREC709());
  unknown->metadata().capture.version = version;
  CHECK(baseline->Observe(17, *unknown) == 0);
  cropped.reset();
  second.reset();
  auto last_sample = make_frame(second_region, 114);
  auto storage = second_region.mapping.GetMemoryAsSpan<uint8_t>();
  storage.back() = 175;
  CHECK(baseline->Observe(17, *last_sample) == 4);
  CHECK(baseline->Observe(17, *last_sample) == 4);
  baseline->Retire();
  CHECK(baseline->Observe(17, *first) == 0);
  CheckTileChanges(512, 256);
  CheckTileChanges(514, 258);
  base::DiscardableMemoryAllocator::SetInstance(nullptr);
  std::puts("ok: independent equal buffers, real I420 collision pair, world, source epoch, restriction, colorspace, crop, unsupported frame and retirement");
}
