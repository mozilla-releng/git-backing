/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* compile-time and runtime tests for whether to use LoongArch-specific
 * extensions. Adapted from js/src/jit/loong64/Architecture-loong64.cpp. */

#include "loongarch.h"

#include <cstdint>
#include <optional>

#if defined(XP_LINUX)
#  if __has_include(<asm/hwcap.h>) && __has_include(<sys/auxv.h>)
#    define USE_HWCAP
#  endif
#endif

#ifdef USE_HWCAP
#  include <asm/hwcap.h>
#  include <sys/auxv.h>
#endif

#if defined(USE_HWCAP)
// the toolchain doesn't yet know about them.
enum LoongArchHwCap : uint64_t {
  LOONG64_HWCAP_LSX = (UINT64_C(1) << 4),
  LOONG64_HWCAP_LASX = (UINT64_C(1) << 5),
};

// Assert the hardcoded feature bits match HWCAP_LOONGARCH_*.
#  ifdef HWCAP_LOONGARCH_LSX
static_assert(HWCAP_LOONGARCH_LSX == LOONG64_HWCAP_LSX);
#  endif
#  ifdef HWCAP_LOONGARCH_LASX
static_assert(HWCAP_LOONGARCH_LASX == LOONG64_HWCAP_LASX);
#  endif
#endif

struct LoongArchCPUFlags {
  uint32_t lsx : 1 = 0;
  uint32_t lasx : 1 = 0;
};

static LoongArchCPUFlags get_loongarch_cpu_flags() {
  // This could be expensive, so cache the result.
  static std::optional<LoongArchCPUFlags> cpu_flags = std::nullopt;

  if (cpu_flags) {  // already checked
    return *cpu_flags;
  }
  cpu_flags = LoongArchCPUFlags();

#if defined(USE_HWCAP)
  // Some features need kernel support for context preservation. These must be
  // detected via HWCAP.
  const uint64_t hwcap = getauxval(AT_HWCAP);

  if (hwcap & LOONG64_HWCAP_LSX) {
    cpu_flags->lsx = true;
  }

  if (hwcap & LOONG64_HWCAP_LASX) {
    cpu_flags->lasx = true;
  }
#endif

  return *cpu_flags;
}

namespace mozilla {
namespace loongarch_private {

bool lsx_enabled = !!(get_loongarch_cpu_flags().lsx);
bool lasx_enabled = !!(get_loongarch_cpu_flags().lasx);

}  // namespace loongarch_private
}  // namespace mozilla
