// CPU-only controls using the production staging helper and real Skia raster images.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "base/at_exit.h"
#include "base/check.h"
#include "gpu/command_buffer/service/webgpu_skia_fallback.h"
#include "third_party/skia/include/core/SkColorSpace.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkPixmap.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace {
constexpr int kWidth = 2;
constexpr int kHeight = 2;
constexpr size_t kStride = 256;
constexpr uint8_t kPadding = 0xa5;
using Bytes = std::array<uint8_t, kStride * kHeight>;
using Pixel8 = std::array<uint8_t, 4>;
using Pixel16 = std::array<uint16_t, 4>;

Bytes Encode8(SkColorType type, SkAlphaType alpha) {
  // Independent packed-byte reference, with red/blue asymmetry and two alpha values.
  std::array<Pixel8, 4> pixels = {{{255, 0, 0, 255}, {0, 173, 67, 255},
                                 {31, 63, 95, 127}, {103, 17, 39, 191}}};
  Bytes bytes;
  bytes.fill(kPadding);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      Pixel8 pixel = pixels[y * kWidth + x];
      if (alpha == kOpaque_SkAlphaType) pixel[3] = 255;
      if (type == kBGRA_8888_SkColorType) std::swap(pixel[0], pixel[2]);
      std::memcpy(bytes.data() + y * kStride + x * 4, pixel.data(), 4);
    }
  }
  return bytes;
}

Bytes Encode16(SkAlphaType alpha) {
  // Exact IEEE binary16 words; include HDR red=2, not an 8-bit quantized reference.
  std::array<Pixel16, 4> pixels = {{{0x4000, 0, 0, 0x3c00},
                                  {0, 0x3800, 0x3400, 0x3c00},
                                  {0x3000, 0x3400, 0x3600, 0x3800},
                                  {0x3400, 0x3000, 0x2c00, 0x3a00}}};
  Bytes bytes;
  bytes.fill(kPadding);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      Pixel16 pixel = pixels[y * kWidth + x];
      if (alpha == kOpaque_SkAlphaType) pixel[3] = 0x3c00;
      std::memcpy(bytes.data() + y * kStride + x * 8, pixel.data(), 8);
    }
  }
  return bytes;
}

void CheckInfo(const SkImageInfo& backing, const SkImageInfo& staging,
               SkColorType expected) {
  CHECK(staging.colorType() == expected);
  CHECK(staging.width() == backing.width() && staging.height() == backing.height());
  CHECK(staging.alphaType() == backing.alphaType());
  CHECK(staging.colorSpace() == backing.colorSpace());
}

struct Counts {
  unsigned reads = 0;
  unsigned writes = 0;
  unsigned old_read_mismatches = 0;
  unsigned old_write_mismatches = 0;
};

void CheckRead8(viz::SharedImageFormat declared, SkColorType declared_type,
                SkColorType backing_type, SkAlphaType alpha,
                const sk_sp<SkColorSpace>& color_space, Counts& counts) {
  const auto backing_info = SkImageInfo::Make(kWidth, kHeight, backing_type, alpha, color_space);
  const Bytes backing = Encode8(backing_type, alpha);
  const Bytes expected = Encode8(declared_type, alpha);
  auto image = SkImages::RasterFromData(
      backing_info, SkData::MakeWithCopy(backing.data(), backing.size()), kStride);
  CHECK(image);
  const auto staging_info = gpu::webgpu::MakeWebGPUFallbackStagingInfo(
      image->imageInfo(), declared);
  CheckInfo(image->imageInfo(), staging_info, declared_type);
  Bytes actual;
  actual.fill(kPadding);
  CHECK(image->readPixels(staging_info, actual.data(), kStride, 0, 0));
  CHECK(actual == expected); // Full pixels plus every byte of row padding.
  ++counts.reads;
  // Execute the previous production expression against opposite channel orders.
  if (declared_type != backing_type) {
    Bytes old;
    old.fill(kPadding);
    CHECK(image->readPixels(image->imageInfo(), old.data(), kStride, 0, 0));
    CHECK(old != expected);
    ++counts.old_read_mismatches;
  }
}

void CheckWrite8(viz::SharedImageFormat declared, SkColorType declared_type,
                 SkColorType backing_type, SkAlphaType alpha,
                 const sk_sp<SkColorSpace>& color_space, Counts& counts) {
  const auto backing_info = SkImageInfo::Make(kWidth, kHeight, backing_type, alpha, color_space);
  const auto output_info = backing_info.makeColorType(kRGBA_8888_SkColorType);
  const Bytes staging = Encode8(declared_type, alpha);
  const Bytes expected = Encode8(kRGBA_8888_SkColorType, alpha);
  auto surface = SkSurfaces::Raster(backing_info);
  CHECK(surface);
  const auto staging_info = gpu::webgpu::MakeWebGPUFallbackStagingInfo(
      surface->imageInfo(), declared);
  CheckInfo(surface->imageInfo(), staging_info, declared_type);
  surface->writePixels(SkPixmap(staging_info, staging.data(), kStride), 0, 0);
  Bytes actual;
  actual.fill(kPadding);
  CHECK(surface->readPixels(output_info, actual.data(), kStride, 0, 0));
  CHECK(actual == expected);
  ++counts.writes;
  if (declared_type != backing_type) {
    auto old_surface = SkSurfaces::Raster(backing_info);
    CHECK(old_surface);
    old_surface->writePixels(SkPixmap(old_surface->imageInfo(), staging.data(), kStride), 0, 0);
    Bytes old;
    old.fill(kPadding);
    CHECK(old_surface->readPixels(output_info, old.data(), kStride, 0, 0));
    CHECK(old != expected);
    ++counts.old_write_mismatches;
  }
}

void CheckF16(SkAlphaType alpha, const sk_sp<SkColorSpace>& color_space, Counts& counts) {
  const auto backing_info = SkImageInfo::Make(
      kWidth, kHeight, kRGBA_F16_SkColorType, alpha, color_space);
  const Bytes expected = Encode16(alpha);
  auto image = SkImages::RasterFromData(
      backing_info, SkData::MakeWithCopy(expected.data(), expected.size()), kStride);
  CHECK(image);
  const auto staging_info = gpu::webgpu::MakeWebGPUFallbackStagingInfo(
      image->imageInfo(), viz::SinglePlaneFormat::kRGBA_F16);
  CheckInfo(image->imageInfo(), staging_info, kRGBA_F16_SkColorType);
  Bytes actual;
  actual.fill(kPadding);
  CHECK(image->readPixels(staging_info, actual.data(), kStride, 0, 0));
  CHECK(actual == expected);
  ++counts.reads;
  // Skia raster surfaces are drawable premultiplied/opaque destinations. The
  // unpremultiplied source is exercised above without inventing an alpha change.
  if (alpha != kUnpremul_SkAlphaType) {
    auto surface = SkSurfaces::Raster(backing_info);
    CHECK(surface);
    const auto write_info = gpu::webgpu::MakeWebGPUFallbackStagingInfo(
        surface->imageInfo(), viz::SinglePlaneFormat::kRGBA_F16);
    CheckInfo(surface->imageInfo(), write_info, kRGBA_F16_SkColorType);
    surface->writePixels(SkPixmap(write_info, expected.data(), kStride), 0, 0);
    actual.fill(kPadding);
    CHECK(surface->readPixels(backing_info, actual.data(), kStride, 0, 0));
    CHECK(actual == expected);
    ++counts.writes;
  }
}
}  // namespace

int main() {
  base::AtExitManager at_exit;
  Counts counts;
  for (const auto& color_space : {SkColorSpace::MakeSRGB(), SkColorSpace::MakeSRGBLinear()}) {
    for (auto alpha : {kOpaque_SkAlphaType, kPremul_SkAlphaType, kUnpremul_SkAlphaType}) {
      for (auto declared : {kRGBA_8888_SkColorType, kBGRA_8888_SkColorType}) {
        const auto format = declared == kRGBA_8888_SkColorType
                                ? viz::SharedImageFormat(viz::SinglePlaneFormat::kRGBA_8888)
                                : viz::SharedImageFormat(viz::SinglePlaneFormat::kBGRA_8888);
        for (auto backing : {kRGBA_8888_SkColorType, kBGRA_8888_SkColorType}) {
          CheckRead8(format, declared, backing, alpha, color_space, counts);
          if (alpha != kUnpremul_SkAlphaType)
            CheckWrite8(format, declared, backing, alpha, color_space, counts);
        }
      }
      CheckF16(alpha, color_space, counts);
    }
  }
  CHECK(counts.reads == 30 && counts.writes == 20);
  CHECK(counts.old_read_mismatches == 12 && counts.old_write_mismatches == 8);
  std::printf("PASS real Skia staging pixels: reads=%u writes=%u old-read-rejected=%u "
              "old-write-rejected=%u; exact RGBA/BGRA/F16/HDR/alpha/colorspace/stride\n",
              counts.reads, counts.writes, counts.old_read_mismatches,
              counts.old_write_mismatches);
}
