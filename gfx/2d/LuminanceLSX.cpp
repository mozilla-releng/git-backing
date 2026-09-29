/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LuminanceLSX.h"

#include <lsxintrin.h>

#include <cstdint>

using namespace mozilla::gfx;

#ifdef IS_BIG_ENDIAN
#  error "No big endian support yet for LoongArch64 LSX"
#endif

/**
 * Byte offsets of channels in a native packed gfxColor or cairo image surface.
 */
#define GFX_ARGB32_OFFSET_A 3
#define GFX_ARGB32_OFFSET_R 2
#define GFX_ARGB32_OFFSET_G 1
#define GFX_ARGB32_OFFSET_B 0

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

  // Byte coefficients in BGRA order; the alpha coefficient is zero.
  alignas(16) const uint8_t coeffBytes[16] = {
      static_cast<uint8_t>(blueFactor), static_cast<uint8_t>(greenFactor),
      static_cast<uint8_t>(redFactor),  0,
      static_cast<uint8_t>(blueFactor), static_cast<uint8_t>(greenFactor),
      static_cast<uint8_t>(redFactor),  0,
      static_cast<uint8_t>(blueFactor), static_cast<uint8_t>(greenFactor),
      static_cast<uint8_t>(redFactor),  0,
      static_cast<uint8_t>(blueFactor), static_cast<uint8_t>(greenFactor),
      static_cast<uint8_t>(redFactor),  0};
  // Alpha bytes of the eight pixels, four from each vector.
  alignas(16) static const uint8_t kAlphaMask[16] = {GFX_ARGB32_OFFSET_A,
                                                     GFX_ARGB32_OFFSET_A + 4,
                                                     GFX_ARGB32_OFFSET_A + 8,
                                                     GFX_ARGB32_OFFSET_A + 12,
                                                     GFX_ARGB32_OFFSET_A + 16,
                                                     GFX_ARGB32_OFFSET_A + 20,
                                                     GFX_ARGB32_OFFSET_A + 24,
                                                     GFX_ARGB32_OFFSET_A + 28,
                                                     0,
                                                     0,
                                                     0,
                                                     0,
                                                     0,
                                                     0,
                                                     0,
                                                     0};
  const __m128i kCoeff = __lsx_vld(coeffBytes, 0);
  const __m128i kAlpha = __lsx_vld(kAlphaMask, 0);
  const __m128i kZero = __lsx_vldi(0);

  for (int32_t y = 0; y < aSize.height; y++) {
    for (int32_t x = 0; x < roundedWidth; x += 8) {
      __m128i va = __lsx_vld(sourcePixel, 0);
      __m128i vb = __lsx_vld(sourcePixel, 16);
      __m128i even0 = __lsx_vmulwev_h_bu(va, kCoeff);
      __m128i odd0 = __lsx_vmulwod_h_bu(va, kCoeff);
      __m128i even1 = __lsx_vmulwev_h_bu(vb, kCoeff);
      __m128i odd1 = __lsx_vmulwod_h_bu(vb, kCoeff);
      __m128i sum0 = __lsx_vadd_h(even0, odd0);
      __m128i sum1 = __lsx_vadd_h(even1, odd1);
      __m128i evenLanes = __lsx_vpickev_h(sum1, sum0);
      __m128i oddLanes = __lsx_vpickod_h(sum1, sum0);
      __m128i gray =
          __lsx_vssrlni_bu_h(kZero, __lsx_vadd_h(evenLanes, oddLanes), 8);

      // Zero out the pixels whose alpha is 0.
      __m128i z = __lsx_vseqi_b(__lsx_vshuf_b(vb, va, kAlpha), 0);
      gray = __lsx_vbitsel_v(gray, kZero, z);

      // Put the result to the 8 pixels
      __lsx_vstelm_d(gray, destPixel, 0, 0);
      sourcePixel += 8 * 4;
      destPixel += 8;
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
