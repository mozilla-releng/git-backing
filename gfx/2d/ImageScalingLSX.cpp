/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <lsxintrin.h>

#include "ImageScaling.h"
#include "mozilla/Attributes.h"

/* The functions below use the following system for averaging 4 pixels:
 *
 * The first observation is that a half-adder is implemented as follows:
 * R = S + 2C or in the case of a and b (a ^ b) + ((a & b) << 1);
 *
 * This can be trivially extended to three pixels by observaring that when
 * doing (a ^ b ^ c) as the sum, the carry is simply the bitwise-or of the
 * carries of the individual numbers, since the sum of 3 bits can only ever
 * have a carry of one.
 *
 * We then observe that the average is then ((carry << 1) + sum) >> 1, or,
 * assuming eliminating overflows and underflows, carry + (sum >> 1).
 *
 * We now average our existing sum with the fourth number, so we get:
 * sum2 = (sum + d) >> 1 or (sum >> 1) + (d >> 1).
 *
 * We now observe that our sum has been moved into place relative to the
 * carry, so we can now average with the carry to get the final 4 input
 * average: avg = (sum2 + carry) >> 1;
 *
 * Or to reverse the proof:
 * avg = ((sum >> 1) + carry + d >> 1) >> 1
 * avg = ((a + b + c) >> 1 + d >> 1) >> 1
 * avg = ((a + b + c + d) >> 2)
 *
 * LSX does not need the SSE "rounded average" trick, since the vavg.bu
 * instruction already truncates (rounds to -inf).
 * <https://jia.je/unofficial-loongarch-intrinsics-guide/lsx/integer_computation/#__m128i-__lsx_vavg_bu-__m128i-a-__m128i-b>
 */

MOZ_ALWAYS_INLINE __m128i avg_lsx_8x2(__m128i a, __m128i b, __m128i c,
                                      __m128i d) {
  // Regroup both rows so that lane i holds the even or odd pixels of a
  // 2x2 block: [a0, a2, b0, b2] and [a1, a3, b1, b3] (lower row likewise).
  __m128i evenUpper = __lsx_vpickev_w(b, a);
  __m128i oddUpper = __lsx_vpermi_w(b, a, 0b11011101);
  __m128i evenLower = __lsx_vpickev_w(d, c);
  __m128i oddLower = __lsx_vpermi_w(d, c, 0b11011101);

  __m128i sum = __lsx_vxor_v(evenUpper, __lsx_vxor_v(oddUpper, evenLower));

  __m128i carry = __lsx_vor_v(__lsx_vand_v(evenUpper, oddUpper),
                              __lsx_vor_v(__lsx_vand_v(evenUpper, evenLower),
                                          __lsx_vand_v(oddUpper, evenLower)));

  sum = __lsx_vavg_bu(sum, oddLower);

  return __lsx_vavg_bu(sum, carry);
}

MOZ_ALWAYS_INLINE __m128i avg_lsx_4x2_4x1(__m128i a, __m128i b) {
  return __lsx_vavg_bu(a, b);
}

MOZ_ALWAYS_INLINE __m128i avg_lsx_8x1_4x1(__m128i a, __m128i b) {
  __m128i t = __lsx_vpermi_w(b, a, 0b11011101);
  b = __lsx_vpickev_w(b, a);
  a = t;

  return __lsx_vavg_bu(a, b);
}

MOZ_ALWAYS_INLINE uint32_t Avg2x2(uint32_t a, uint32_t b, uint32_t c,
                                  uint32_t d) {
  uint32_t sum = a ^ b ^ c;
  uint32_t carry = (a & b) | (a & c) | (b & c);

  constexpr uint32_t Mask = 0xfefefefe;

  // Not having a byte based average instruction means we should mask to avoid
  // underflow.
  sum = (((sum ^ d) & Mask) >> 1) + (sum & d);

  return (((sum ^ carry) & Mask) >> 1) + (sum & carry);
}

// Simple 2 pixel average version of the function above.
MOZ_ALWAYS_INLINE uint32_t Avg2(uint32_t a, uint32_t b) {
  uint32_t sum = a ^ b;
  uint32_t carry = (a & b);

  constexpr uint32_t Mask = 0xfefefefe;

  return ((sum & Mask) >> 1) + carry;
}

namespace mozilla::gfx {

void ImageHalfScaler::HalfImage2D_LSX(uint8_t* aSource, int32_t aSourceStride,
                                      const IntSize& aSourceSize,
                                      uint8_t* aDest, uint32_t aDestStride) {
  const int Bpp = 4;

  for (int y = 0; y < aSourceSize.height; y += 2) {
    __m128i* storage = (__m128i*)(aDest + (y / 2) * aDestStride);
    int x = 0;
    // LSX loads and stores tolerate any alignment.
    for (; x < (aSourceSize.width - 7); x += 8) {
      const uint8_t* upperRow = aSource + (y * aSourceStride + x * Bpp);
      const uint8_t* lowerRow = aSource + ((y + 1) * aSourceStride + x * Bpp);

      __m128i a = __lsx_vld(upperRow, 0);
      __m128i b = __lsx_vld(upperRow, 16);
      __m128i c = __lsx_vld(lowerRow, 0);
      __m128i d = __lsx_vld(lowerRow, 16);

      *storage++ = avg_lsx_8x2(a, b, c, d);
    }

    uint32_t* unalignedStorage = (uint32_t*)storage;
    // Take care of the final pixels, we know there's an even number of pixels
    // in the source rectangle. We use a 2x2 'simd' implementation for this.
    //
    // Potentially we only have to do this in the last row since overflowing
    // 8 pixels in an earlier row would appear to be harmless as it doesn't
    // touch invalid memory. Even when reading and writing to the same surface.
    // in practice we only do this when doing an additional downscale pass, and
    // in this situation we have unused stride to write into harmlessly.
    // I do not believe the additional code complexity would be worth it though.
    for (; x < aSourceSize.width; x += 2) {
      uint8_t* upperRow = aSource + (y * aSourceStride + x * Bpp);
      uint8_t* lowerRow = aSource + ((y + 1) * aSourceStride + x * Bpp);

      *unalignedStorage++ =
          Avg2x2(*(uint32_t*)upperRow, *((uint32_t*)upperRow + 1),
                 *(uint32_t*)lowerRow, *((uint32_t*)lowerRow + 1));
    }
  }
}

void ImageHalfScaler::HalfImageVertical_LSX(uint8_t* aSource,
                                            int32_t aSourceStride,
                                            const IntSize& aSourceSize,
                                            uint8_t* aDest,
                                            uint32_t aDestStride) {
  for (int y = 0; y < aSourceSize.height; y += 2) {
    __m128i* storage = (__m128i*)(aDest + (y / 2) * aDestStride);
    int x = 0;
    for (; x < (aSourceSize.width - 3); x += 4) {
      const uint8_t* upperRow = aSource + (y * aSourceStride + x * 4);
      const uint8_t* lowerRow = aSource + ((y + 1) * aSourceStride + x * 4);

      __m128i a = __lsx_vld(upperRow, 0);
      __m128i b = __lsx_vld(lowerRow, 0);

      *storage++ = avg_lsx_4x2_4x1(a, b);
    }

    uint32_t* unalignedStorage = (uint32_t*)storage;
    // Take care of the final pixels, we know there's an even number of pixels
    // in the source rectangle.
    //
    // Similar overflow considerations are valid as in the previous function.
    for (; x < aSourceSize.width; x++) {
      uint8_t* upperRow = aSource + (y * aSourceStride + x * 4);
      uint8_t* lowerRow = aSource + ((y + 1) * aSourceStride + x * 4);

      *unalignedStorage++ = Avg2(*(uint32_t*)upperRow, *(uint32_t*)lowerRow);
    }
  }
}

void ImageHalfScaler::HalfImageHorizontal_LSX(uint8_t* aSource,
                                              int32_t aSourceStride,
                                              const IntSize& aSourceSize,
                                              uint8_t* aDest,
                                              uint32_t aDestStride) {
  for (int y = 0; y < aSourceSize.height; y++) {
    __m128i* storage = (__m128i*)(aDest + y * aDestStride);
    int x = 0;
    for (; x < (aSourceSize.width - 7); x += 8) {
      const uint8_t* pixels = aSource + (y * aSourceStride + x * 4);

      __m128i a = __lsx_vld(pixels, 0);
      __m128i b = __lsx_vld(pixels, 16);

      *storage++ = avg_lsx_8x1_4x1(a, b);
    }

    uint32_t* unalignedStorage = (uint32_t*)storage;
    // Take care of the final pixels, we know there's an even number of pixels
    // in the source rectangle.
    //
    // Similar overflow considerations are valid as in the previous function.
    for (; x < aSourceSize.width; x += 2) {
      uint32_t* pixels = (uint32_t*)(aSource + (y * aSourceStride + x * 4));

      *unalignedStorage++ = Avg2(*pixels, *(pixels + 1));
    }
  }
}

}  // namespace mozilla::gfx
