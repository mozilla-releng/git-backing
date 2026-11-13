/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "sandboxTarget.h"

#include "mozilla/CpuInfo.h"
#include "mozilla/SandboxSettings.h"
#include "mozilla/WindowsStackWalkInitialization.h"
#include "sandbox/win/src/sandbox.h"

namespace mozilla {

// We need to define this function out of line so that clang-cl doesn't inline
// it.
/* static */
SandboxTarget* SandboxTarget::Instance() {
  static SandboxTarget sb;
  return &sb;
}

void SandboxTarget::StartSandbox() {
  if (mTargetServices) {
#if defined(_M_AMD64) || defined(_M_ARM64)
    // Hooks must be installed now even though no stack walk has been requested
    // yet, in case delayed mitigations against dynamic code are used.
    InstallStackWalkSuppressionHooks();
#endif  // _M_AMD64 || _M_ARM64

    mTargetServices->LowerToken();
    NotifyStartObservers();
  }
}

#if defined(_M_X64)
static bool IsX64EmulatedOnArm64() {
  using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
  auto isWow64Process2 = reinterpret_cast<IsWow64Process2Fn>(
      ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
  if (!isWow64Process2) {
    return false;
  }

  USHORT processMachine;
  USHORT nativeMachine;
  return isWow64Process2(::GetCurrentProcess(), &processMachine,
                         &nativeMachine) &&
         nativeMachine == IMAGE_FILE_MACHINE_ARM64;
}
#endif  // _M_X64

void SandboxTarget::LowerContentSandbox() {
  if (GetEffectiveContentSandboxLevel() > 7) {
    // Libraries required by Network Security Services (NSS).
    ::LoadLibraryW(L"freebl3.dll");
    ::LoadLibraryW(L"softokn3.dll");
#if defined(_M_X64)
    // When emulated on ARM64 the loader uses ARM64EC entry points that bypass
    // our interceptions, so late loads from System32 fail.
    if (IsX64EmulatedOnArm64()) {
      ::LoadLibraryW(L"TextShaping.dll");
    }
#endif  // _M_X64
    // Cache value that is retrieved from a registry entry.
    (void)GetCpuFrequencyMHz();
  }

  StartSandbox();
}

void SandboxTarget::NotifyStartObservers() {
  for (auto&& obs : mStartObservers) {
    if (!obs) {
      continue;
    }

    obs();
  }

  mStartObservers.clear();
}

}  // namespace mozilla
