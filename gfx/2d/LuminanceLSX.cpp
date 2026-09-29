/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LuminanceLSX.h"

#include <lsxintrin.h>

#include <cstdint>

using namespace mozilla::gfx;

/**
 * Byte offsets of channels in a native packed gfxColor or cairo image surface.
 */
#ifdef IS_BIG_ENDIAN
#  define GFX_ARGB32_OFFSET_A 0
#  define GFX_ARGB32_OFFSET_R 1
#  define GFX_ARGB32_OFFSET_G 2
#  define GFX_ARGB32_OFFSET_B 3
#else
#  define GFX_ARGB32_OFFSET_A 3
#  define GFX_ARGB32_OFFSET_R 2
#  define GFX_ARGB32_OFFSET_G 1
#  define GFX_ARGB32_OFFSET_B 0
#endif

typedef uint8_t uint8x16_t __attribute__((vector_size(16)));

static inline __m128i MakeSel(uint8_t c) {
  const uint8x16_t sel = {c,
                          static_cast<uint8_t>(c + 4),
                          static_cast<uint8_t>(c + 8),
                          static_cast<uint8_t>(c + 12),
                          static_cast<uint8_t>(c + 16),
                          static_cast<uint8_t>(c + 20),
                          static_cast<uint8_t>(c + 24),
                          static_cast<uint8_t>(c + 28),
                          0,
                          0,
                          0,
                          0,
                          0,
                          0,
                          0,
                          0};
  return reinterpret_cast<const __m128i>(sel);
};

void ComputesRGBLuminanceMask_LSX(const uint8_t* aSourceData,
                                  int32_t aSourceStride, uint8_t* aDestData,
                                  int32_t aDestStride,
                                  const mozilla::gfx::IntSize& aSize,
                                  float aOpacity) {
  const int32_t redFactor = 55 * aOpacity;     // 255 * 0.2125 * opacity
  const int32_t greenFactor = 183 * aOpacity;  // 255 * 0.7154 * opacity
  const int32_t blueFactor = 18 * aOpacity;    // 255 * 0.0721
  const uint8_t* sourcePixel = aSourceData;
  uint8_t* destPixel = aDestData;
  int32_t destOffset = aDestStride - aSize.width;
  int32_t sourceOffset = aSourceStride - 4 * aSize.width;
  int32_t remainderWidth = aSize.width % 16;
  int32_t roundedWidth = aSize.width - remainderWidth;

  // Per-channel selector constants (BGRA layout): lanes 0-3 pick bytes
  // 0,4,8,12 of the second vector (pixels 4-7), lanes 4-7 pick bytes
  // (selector % 16) of the first vector (pixels 0-3).
  const __m128i kSelB = MakeSel(GFX_ARGB32_OFFSET_B);
  const __m128i kSelG = MakeSel(GFX_ARGB32_OFFSET_G);
  const __m128i kSelR = MakeSel(GFX_ARGB32_OFFSET_R);
  const __m128i kSelA = MakeSel(GFX_ARGB32_OFFSET_A);
  const __m128i kRed = __lsx_vreplgr2vr_h(static_cast<int16_t>(redFactor));
  const __m128i kGreen = __lsx_vreplgr2vr_h(static_cast<int16_t>(greenFactor));
  const __m128i kBlue = __lsx_vreplgr2vr_h(static_cast<int16_t>(blueFactor));
  const __m128i kZero = __lsx_vldi(0);

  // Eight pixels of source data -> grayscale bytes in the low half.
  const auto pairGray = [&](const uint8_t* src) {
    __m128i va = __lsx_vld(src, 0);
    __m128i vb = __lsx_vld(src, 16);

    __m128i B01 = __lsx_vshuf_b(vb, va, kSelB);
    __m128i G01 = __lsx_vshuf_b(vb, va, kSelG);
    __m128i R01 = __lsx_vshuf_b(vb, va, kSelR);
    __m128i A01 = __lsx_vshuf_b(vb, va, kSelA);

    __m128i Bw = __lsx_vsllwil_hu_bu(B01, 0);
    __m128i Gw = __lsx_vsllwil_hu_bu(G01, 0);
    __m128i Rw = __lsx_vsllwil_hu_bu(R01, 0);

    __m128i temp = __lsx_vmul_h(Rw, kRed);
    temp = __lsx_vadd_h(temp, __lsx_vmul_h(Gw, kGreen));
    temp = __lsx_vadd_h(temp, __lsx_vmul_h(Bw, kBlue));
    // Narrow all 8 h-lanes with a plain (non-rounding) shift.
    __m128i gray = __lsx_vssrlni_bu_h(temp, temp, 8);

    // Zero out the pixels whose alpha is 0.
    __m128i z = __lsx_vseqi_b(A01, 0);
    gray = __lsx_vbitsel_v(kZero, gray, __lsx_vnor_v(z, z));
    return gray;
  };

  for (int32_t y = 0; y < aSize.height; y++) {
    for (int32_t x = 0; x < roundedWidth; x += 16) {
      __m128i g01 = pairGray(sourcePixel);
      __m128i g23 = pairGray(sourcePixel + 32);
      __m128i gray = __lsx_vpermi_w(g23, g01, 0b01000100);
      // Put the result to the 16 pixels
      __lsx_vst(gray, destPixel, 0);
      sourcePixel += 16 * 4;
      destPixel += 16;
    }

    // Calculate the rest pixels of the line by cpu.
    for (int32_t x = 0; x < remainderWidth; x++) {
      if (sourcePixel[GFX_ARGB32_OFFSET_A] > 0) {
        *destPixel = (redFactor * sourcePixel[GFX_ARGB32_OFFSET_R] +
                      greenFactor * sourcePixel[GFX_ARGB32_OFFSET_G] +
                      blueFactor * sourcePixel[GFX_ARGB32_OFFSET_B]) >>
                     8;
      } else {
        *destPixel = 0;
      }
      sourcePixel += 4;
      destPixel++;
    }
    sourcePixel += sourceOffset;
    destPixel += destOffset;
  }
}
