// Use of this source code is governed by a BSD-style license that can be
// found in the gfx/skia/LICENSE file.

#include <lsxintrin.h>

#include "SkConvolver.h"
#include "mozilla/Attributes.h"

namespace skia {

static MOZ_ALWAYS_INLINE void AccumRemainder(
    const unsigned char* pixelsLeft,
    const SkConvolutionFilter1D::ConvolutionFixed* filterValues, __m128i& accum,
    int r) {
  int remainder[4] = {0};
  for (int i = 0; i < r; i++) {
    SkConvolutionFilter1D::ConvolutionFixed coeff = filterValues[i];
    remainder[0] += coeff * pixelsLeft[i * 4 + 0];
    remainder[1] += coeff * pixelsLeft[i * 4 + 1];
    remainder[2] += coeff * pixelsLeft[i * 4 + 2];
    remainder[3] += coeff * pixelsLeft[i * 4 + 3];
  }
  __m128i t = __lsx_vld(reinterpret_cast<const uint8_t*>(remainder), 0);
  accum = __lsx_vadd_w(accum, t);
}

// Byte-shuffle mask: duplicate the low half's first two bytes across lanes
// 0-3 and the high half's first two bytes across lanes 4-7.
alignas(16) static const uint8_t kCoeffDupMask[16] = {
    0, 1, 0, 1, 0, 1, 0, 1, 16, 17, 16, 17, 16, 17, 16, 17};

// Convolves horizontally along a single row. The row data is given in
// |srcData| and continues for the numValues() of the filter.
void convolve_horizontally_lsx(const unsigned char* srcData,
                               const SkConvolutionFilter1D& filter,
                               unsigned char* outRow, bool /*hasAlpha*/) {
  constexpr uint32_t RoundImm = 1 << (SkConvolutionFilter1D::kShiftBits - 1);
  constexpr int32_t RoundImmVldi = -0xee0;
  static_assert(((static_cast<uint32_t>(RoundImmVldi) & 0xFF) << 8) ==
                RoundImm);

  // Output one pixel each iteration, calculating all channels (RGBA) together.
  int numValues = filter.numValues();
  for (int outX = 0; outX < numValues; outX++) {
    // Get the filter that determines the current output pixel.
    int filterOffset, filterLength;
    const SkConvolutionFilter1D::ConvolutionFixed* filterValues =
        filter.FilterForValue(outX, &filterOffset, &filterLength);

    // Compute the first pixel in this row that the filter affects. It will
    // touch |filterLength| pixels (4 bytes each) after this.
    const unsigned char* rowToFilter = &srcData[filterOffset * 4];

    const __m128i zero = __lsx_vldi(0);
    __m128i accumEven01 = zero;
    __m128i accumOdd01 = zero;
    __m128i accumEven23 = zero;
    __m128i accumOdd23 = zero;

    // We will load and accumulate with four coefficients per iteration.
    for (int filterX = 0; filterX < filterLength >> 2; filterX++) {
      // Load 4 coefficients => duplicate 1st and 2nd of them for all channels.
      // [16] c1 c1 c1 c1 c0 c0 c0 c0
      __m128i coeff01 = __lsx_vshuf_b(__lsx_vldrepl_h(filterValues + 1, 0),
                                      __lsx_vldrepl_h(filterValues + 0, 0),
                                      __lsx_vld(kCoeffDupMask, 0));

      // Load four pixels => unpack the first two pixels to 16 bits =>
      // multiply with coefficients => accumulate the convolution result.
      // [8] a3 b3 g3 r3 a2 b2 g2 r2 a1 b1 g1 r1 a0 b0 g0 r0
      __m128i src8 = __lsx_vld(rowToFilter, 0);
      // [16] a1 b1 g1 r1 a0 b0 g0 r0
      __m128i src16 = __lsx_vsllwil_hu_bu(src8, 0);
      // The even and odd 16-bit lanes hold the full 32-bit products of the
      // even and odd channels. The later vilvl/vilvh recombine them per pixel.
      accumEven01 = __lsx_vmaddwev_w_h(accumEven01, src16, coeff01);
      accumOdd01 = __lsx_vmaddwod_w_h(accumOdd01, src16, coeff01);

      // Duplicate 3rd and 4th coefficients for all channels =>
      // unpack the 3rd and 4th pixels to 16 bits => multiply with coefficients
      // => accumulate the convolution results.
      // [16] c3 c3 c3 c3 c2 c2 c2 c2
      __m128i coeff23 = __lsx_vshuf_b(__lsx_vldrepl_h(filterValues + 3, 0),
                                      __lsx_vldrepl_h(filterValues + 2, 0),
                                      __lsx_vld(kCoeffDupMask, 0));
      // [16] a3 g3 b3 r3 a2 g2 b2 r2
      src16 = __lsx_vexth_hu_bu(src8);
      accumEven23 = __lsx_vmaddwev_w_h(accumEven23, src16, coeff23);
      accumOdd23 = __lsx_vmaddwod_w_h(accumOdd23, src16, coeff23);

      // Advance the pixel and coefficients pointers.
      rowToFilter += 16;
      filterValues += 4;
    }

    __m128i accumEven = __lsx_vadd_w(accumEven01, accumEven23);
    __m128i accumOdd = __lsx_vadd_w(accumOdd01, accumOdd23);
    __m128i accum = __lsx_vadd_w(__lsx_vilvl_w(accumOdd, accumEven),
                                 __lsx_vilvh_w(accumOdd, accumEven));

    // When |filterLength| is not divisible by 4, we accumulate the last 1 - 3
    // coefficients one at a time.
    int r = filterLength & 3;
    if (r) {
      int remainderOffset = (filterOffset + filterLength - r) * 4;
      AccumRemainder(srcData + remainderOffset, filterValues, accum, r);
    }

    // Shift right for fixed point implementation, with rounding.
    __m128i round = __lsx_vldi(RoundImmVldi);
    accum = __lsx_vssrani_h_w(zero, __lsx_vadd_w(accum, round),
                              SkConvolutionFilter1D::kShiftBits);

    // Packing 16 bits |accum| to 8 bits per channel (unsigned saturation).
    accum = __lsx_vssrarni_bu_h(zero, accum, 0);

    // Store the pixel value of 32 bits.
    int32_t out = static_cast<int32_t>(__lsx_vpickve2gr_w(accum, 0));
    memcpy(outRow, &out, 4);
    outRow += 4;
  }
}

// Does vertical convolution to produce one output row. The filter values and
// length are given in the first two parameters. These are applied to each
// of the rows pointed to in the |sourceDataRows| array, with each row
// being |pixelWidth| wide.
//
// The output must have room for |pixelWidth * 4| bytes.
template <bool hasAlpha>
static void ConvolveVertically(
    const SkConvolutionFilter1D::ConvolutionFixed* filterValues,
    int filterLength, unsigned char* const* sourceDataRows, int pixelWidth,
    unsigned char* outRow) {
  constexpr uint32_t RoundImm = 1 << (SkConvolutionFilter1D::kShiftBits - 1);
  constexpr int32_t RoundImmVldi = -0xee0;
  static_assert(((static_cast<uint32_t>(RoundImmVldi) & 0xFF) << 8) ==
                RoundImm);

  // Output four pixels per iteration (16 bytes).
  int width = pixelWidth & ~3;
  const __m128i zero = __lsx_vldi(0);
  for (int outX = 0; outX < width; outX += 4) {
    // The even and odd 16-bit lanes hold the full 32-bit products of the even
    // and odd channels. The later vilvl/vilvh recombine them per pixel.
    __m128i accumEven01 = zero;
    __m128i accumOdd01 = zero;
    __m128i accumEven23 = zero;
    __m128i accumOdd23 = zero;

    // Convolve with one filter coefficient per iteration.
    for (int filterY = 0; filterY < filterLength; filterY++) {
      // Duplicate the filter coefficient 8 times.
      // [16] cj cj cj cj cj cj cj cj
      __m128i coeff16 = __lsx_vreplgr2vr_h(filterValues[filterY]);

      // Load four pixels (16 bytes) together.
      // [8] a3 b3 g3 r3 a2 b2 g2 r2 a1 b1 g1 r1 a0 b0 g0 r0
      __m128i src8 = __lsx_vld(&sourceDataRows[filterY][outX << 2], 0);

      // Unpack 1st and 2nd pixels from 8 bits to 16 bits for each channels =>
      // multiply with current coefficient => accumulate the result.
      // [16] a1 b1 g1 r1 a0 b0 g0 r0
      __m128i src16 = __lsx_vsllwil_hu_bu(src8, 0);
      accumEven01 = __lsx_vmaddwev_w_h(accumEven01, src16, coeff16);
      accumOdd01 = __lsx_vmaddwod_w_h(accumOdd01, src16, coeff16);

      // Unpack 3rd and 4th pixels from 8 bits to 16 bits for each channels =>
      // multiply with current coefficient => accumulate the result.
      // [16] a3 b3 g3 r3 a2 b2 g2 r2
      src16 = __lsx_vexth_hu_bu(src8);
      accumEven23 = __lsx_vmaddwev_w_h(accumEven23, src16, coeff16);
      accumOdd23 = __lsx_vmaddwod_w_h(accumOdd23, src16, coeff16);
    }

    // [32] a0 b0 g0 r0
    __m128i accum0 = __lsx_vilvl_w(accumOdd01, accumEven01);
    // [32] a1 b1 g1 r1
    __m128i accum1 = __lsx_vilvh_w(accumOdd01, accumEven01);
    // [32] a2 b2 g2 r2
    __m128i accum2 = __lsx_vilvl_w(accumOdd23, accumEven23);
    // [32] a3 b3 g3 r3
    __m128i accum3 = __lsx_vilvh_w(accumOdd23, accumEven23);

    // Shift right for fixed point implementation, with rounding, and pack
    // 32 bits |accum| to 16 bits per channel (signed saturation).
    __m128i round = __lsx_vldi(RoundImmVldi);
    // [16] a1 b1 g1 r1 a0 b0 g0 r0
    accum0 = __lsx_vssrani_h_w(__lsx_vadd_w(accum1, round),
                               __lsx_vadd_w(accum0, round),
                               SkConvolutionFilter1D::kShiftBits);
    // [16] a3 b3 g3 r3 a2 b2 g2 r2
    accum2 = __lsx_vssrani_h_w(__lsx_vadd_w(accum3, round),
                               __lsx_vadd_w(accum2, round),
                               SkConvolutionFilter1D::kShiftBits);

    // Packing 16 bits |accum| to 8 bits per channel (unsigned saturation).
    // [8] a3 b3 g3 r3 a2 b2 g2 r2 a1 b1 g1 r1 a0 b0 g0 r0
    accum0 = __lsx_vssrarni_bu_h(accum2, accum0, 0);

    if constexpr (hasAlpha) {
      // Compute the max(ri, gi, bi) for each pixel.
      // [8] xx a3 b3 g3 xx a2 b2 g2 xx a1 b1 g1 xx a0 b0 g0
      __m128i a = __lsx_vsrli_w(accum0, 8);
      // [8] xx xx xx max3 xx xx xx max2 xx xx xx max1 xx xx xx max0
      __m128i b = __lsx_vmax_bu(a, accum0);  // Max of r and g.
      // [8] xx xx a3 b3 xx xx a2 b2 xx xx a1 b1 xx xx a0 b0
      a = __lsx_vsrli_w(accum0, 16);
      // [8] xx xx xx max3 xx xx xx max2 xx xx xx max1 xx xx xx max0
      b = __lsx_vmax_bu(a, b);  // Max of r and g and b.
      // [8] max3 00 00 00 max2 00 00 00 max1 00 00 00 max0 00 00 00
      b = __lsx_vslli_w(b, 24);

      // Make sure the value of alpha channel is always larger than maximum
      // value of color channels.
      accum0 = __lsx_vmax_bu(b, accum0);
    } else {
      // Set value of alpha channels to 0xFF.
      __m128i mask = __lsx_vslli_w(__lsx_vreplgr2vr_w(0xFF), 24);
      accum0 = __lsx_vor_v(accum0, mask);
    }

    // Store the convolution result (16 bytes) and advance the pixel pointers.
    __lsx_vst(accum0, outRow, 0);
    outRow += 16;
  }

  // When the width of the output is not divisible by 4, we need to save one
  // pixel (4 bytes) each time. And also the fourth pixel is always absent.
  int r = pixelWidth & 3;
  if (r) {
    __m128i accum0 = zero;
    __m128i accum1 = zero;
    __m128i accum2 = zero;
    for (int filterY = 0; filterY < filterLength; ++filterY) {
      __m128i coeff16 = __lsx_vreplgr2vr_h(filterValues[filterY]);
      // [8] a3 b3 g3 r3 a2 b2 g2 r2 a1 b1 g1 r1 a0 b0 g0 r0
      __m128i src8 = __lsx_vld(&sourceDataRows[filterY][width << 2], 0);
      // [16] a1 b1 g1 r1 a0 b0 g0 r0
      __m128i src16 = __lsx_vsllwil_hu_bu(src8, 0);
      __m128i mul_wev = __lsx_vmulwev_w_h(src16, coeff16);
      __m128i mul_wod = __lsx_vmulwod_w_h(src16, coeff16);
      // [32] a0 b0 g0 r0
      accum0 = __lsx_vadd_w(accum0, __lsx_vilvl_w(mul_wod, mul_wev));
      // [32] a1 b1 g1 r1
      accum1 = __lsx_vadd_w(accum1, __lsx_vilvh_w(mul_wod, mul_wev));
      // [16] a3 b3 g3 r3 a2 b2 g2 r2
      src16 = __lsx_vexth_hu_bu(src8);
      mul_wev = __lsx_vmulwev_w_h(src16, coeff16);
      mul_wod = __lsx_vmulwod_w_h(src16, coeff16);
      // [32] a2 b2 g2 r2
      accum2 = __lsx_vadd_w(accum2, __lsx_vilvl_w(mul_wod, mul_wev));
    }

    __m128i round = __lsx_vldi(RoundImmVldi);
    accum0 = __lsx_vsrai_w(__lsx_vadd_w(accum0, round),
                           SkConvolutionFilter1D::kShiftBits);
    accum1 = __lsx_vsrai_w(__lsx_vadd_w(accum1, round),
                           SkConvolutionFilter1D::kShiftBits);
    accum2 = __lsx_vsrai_w(__lsx_vadd_w(accum2, round),
                           SkConvolutionFilter1D::kShiftBits);
    // [16] a1 b1 g1 r1 a0 b0 g0 r0
    accum0 = __lsx_vssrani_h_w(accum1, accum0, 0);
    // [16] a3 b3 g3 r3 a2 b2 g2 r2
    accum2 = __lsx_vssrani_h_w(zero, accum2, 0);
    // [8] a3 b3 g3 r3 a2 b2 g2 r2 a1 b1 g1 r1 a0 b0 g0 r0
    accum0 = __lsx_vssrarni_bu_h(accum2, accum0, 0);
    if (hasAlpha) {
      // [8] xx a3 b3 g3 xx a2 b2 g2 xx a1 b1 g1 xx a0 b0 g0
      __m128i a = __lsx_vsrli_w(accum0, 8);
      // [8] xx xx xx max3 xx xx xx max2 xx xx xx max1 xx xx xx max0
      __m128i b = __lsx_vmax_bu(a, accum0);
      // [8] xx xx a3 b3 xx xx a2 b2 xx xx a1 b1 xx xx a0 b0
      a = __lsx_vsrli_w(accum0, 16);
      // [8] xx xx xx max3 xx xx xx max2 xx xx xx max1 xx xx xx max0
      b = __lsx_vmax_bu(a, b);
      // [8] max3 00 00 00 max2 00 00 00 max1 00 00 00 max0 00 00 00
      b = __lsx_vslli_w(b, 24);
      accum0 = __lsx_vmax_bu(b, accum0);
    } else {
      // Zero-extend (-0xc01) & 0xFF to u32, left-shift by 24 places, then
      // broadcast.
      __m128i mask = __lsx_vldi(-0xc01);
      accum0 = __lsx_vor_v(accum0, mask);
    }

    for (int i = 0; i < r; i++) {
      *(reinterpret_cast<int32_t*>(outRow)) = __lsx_vpickve2gr_w(accum0, 0);
      accum0 = __lsx_vbsrl_v(accum0, 4);
      outRow += 4;
    }
  }
}

void convolve_vertically_lsx(
    const SkConvolutionFilter1D::ConvolutionFixed* filterValues,
    int filterLength, unsigned char* const* sourceDataRows, int pixelWidth,
    unsigned char* outRow, bool hasAlpha) {
  if (hasAlpha) {
    ConvolveVertically<true>(filterValues, filterLength, sourceDataRows,
                             pixelWidth, outRow);
  } else {
    ConvolveVertically<false>(filterValues, filterLength, sourceDataRows,
                              pixelWidth, outRow);
  }
}

}  // namespace skia
