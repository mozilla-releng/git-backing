/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file should only be compiled if you're on LoongArch64.

#include <lsxintrin.h>

#include "CharacterDataBufferImpl.h"
#include "nscore.h"

namespace mozilla {
namespace LSX {

int32_t FirstNon8Bit(const char16_t* str, const char16_t* end) {
  using p = Non8BitParameters<sizeof(size_t)>;
  const size_t mask = p::mask();
  const uint32_t numUnicharsPerWord = p::numUnicharsPerWord();

  const uint32_t len = end - str;

  uint32_t i = 0;

  // Check 8 unichars at a time.
  const uint32_t vectWalkEnd = (len / 8) * 8;
  // Zero-extend  0x01 to 16-bit, then shift left by 8 bits, then broadcast.
  const __m128i kVec0x100u16x8 = __lsx_vldi(-0xaff);
  for (; i < vectWalkEnd; i += 8) {
    __m128i vect;
    memcpy(&vect, str + i, sizeof(vect));
    // vslt_hu yields all-ones where the unit is <= 0xFF, so vmskltz_h yields a
    // bit per unichar set for the ASCII-range units; invert it to find units
    // above 255 and return the start of the first batch containing one.
    uint32_t asciiMask = __lsx_vpickve2gr_hu(
        __lsx_vmskltz_h(__lsx_vslt_hu(vect, kVec0x100u16x8)), 0);
    if ((~asciiMask) & 0xFF) {
      return int32_t(i);
    }
  }

  // Check one word at a time.
  const uint32_t wordWalkEnd =
      ((len - i) / numUnicharsPerWord) * numUnicharsPerWord;
  for (; i < wordWalkEnd; i += numUnicharsPerWord) {
    const size_t word = *reinterpret_cast<const size_t*>(str + i);
    if (word & mask) return int32_t(i);
  }

  // Take care of the remainder one character at a time.
  for (; i < len; i++) {
    if (str[i] > 255) {
      return int32_t(i);
    }
  }

  return -1;
}

}  // namespace LSX
}  // namespace mozilla
