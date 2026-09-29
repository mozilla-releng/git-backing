/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef LUMINANCE_LSX_H_
#define LUMINANCE_LSX_H_

#include "mozilla/gfx/Point.h"

void ComputesRGBLuminanceMask_LSX(const uint8_t* aSourceData,
                                  int32_t aSourceStride, uint8_t* aDestData,
                                  int32_t aDestStride,
                                  const mozilla::gfx::IntSize& aSize,
                                  float aOpacity);

#endif /* LUMINANCE_LSX_H_ */
