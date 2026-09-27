/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* compile-time and runtime tests for whether to use LoongArch-specific
 * extensions */

#ifndef mozilla_loongarch_h_
#define mozilla_loongarch_h_

// for definition of MFBT_DATA
#include "mozilla/Types.h"

namespace mozilla {

namespace loongarch_private {
extern bool MFBT_DATA lsx_enabled;
extern bool MFBT_DATA lasx_enabled;
}  // namespace loongarch_private

inline bool supports_lsx() {
#ifdef __loongarch__
  return loongarch_private::lsx_enabled;
#else
  return false;
#endif
}

inline bool supports_lasx() {
#ifdef __loongarch__
  return loongarch_private::lasx_enabled;
#else
  return false;
#endif
}

}  // namespace mozilla

#endif /* ifndef mozilla_loongarch_h_ */
