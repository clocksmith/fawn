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
void CheckSrgb(const scoped_refptr<media::VideoFrame>& frame, uint8_t blue) {
  auto size = frame->coded_size();
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
  const std::array<uint8_t,4> expected{114,5,blue,255};
  for (size_t i=0; i<pixels.size(); ++i)
    CHECK(pixels[i] == expected[i%4]);
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
  base::DiscardableMemoryAllocator::SetInstance(nullptr);
  std::puts("ok: independent equal buffers, real I420 collision pair, world, source epoch, restriction, colorspace, crop, unsupported frame and retirement");
}
