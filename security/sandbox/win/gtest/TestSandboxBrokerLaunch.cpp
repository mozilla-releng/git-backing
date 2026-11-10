/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <windows.h>

#include <string>

#include "gtest/gtest.h"
#include "mozilla/glean/SecuritySandboxMetrics.h"
#include "mozilla/glean/fog_ffi_generated.h"
#include "nsString.h"
#include "nsTArray.h"
#include "sandbox/win/src/sandbox_types.h"

// clang-format off
#include "../src/sandboxbroker/sandboxBroker.h"
// clang-format on

namespace mozilla {
namespace {

class SandboxBrokerLaunchTelemetry : public ::testing::Test {
 protected:
  void SetUp() override {
    nsCString empty;
    ASSERT_EQ(NS_OK, glean::impl::fog_test_reset(&empty, &empty));
  }
};

// Launches aPath as a sandboxed generic utility process, which is expected to
// fail.
void LaunchExpectingFailure(const wchar_t* aPath) {
  SandboxBroker broker;
  ASSERT_TRUE(broker.SetSecurityLevelForUtilityProcess(
      ipc::SandboxingKind::GENERIC_UTILITY));
  base::EnvironmentMap env;
  void* processHandle = nullptr;
  auto result = broker.LaunchApp(aPath, L"", env, GeckoProcessType_Utility,
                                 false, nullptr, &processHandle);
  ASSERT_TRUE(result.isErr());
}

template <typename LabeledCounter>
int32_t GetCount(const LabeledCounter& aMetric) {
  return aMetric.Get("utility"_ns).TestGetValue().unwrap().valueOr(0);
}

nsCString GetExtra(const glean::impl::RecordedEvent& aEvent, const char* aKey) {
  for (const auto& extra : aEvent.mExtra) {
    if (std::get<0>(extra).Equals(aKey)) {
      return std::get<1>(extra);
    }
  }
  return "<missing>"_ns;
}

void ExpectFailedCreateProcessEvent(const glean::impl::RecordedEvent& aEvent,
                                    DWORD aWin32Error) {
  EXPECT_STREQ("utility", GetExtra(aEvent, "process_type").get());
  EXPECT_EQ(nsPrintfCString("%d", ::sandbox::SBOX_ERROR_CREATE_PROCESS),
            GetExtra(aEvent, "result_code"));
  EXPECT_EQ(nsPrintfCString("%lu", aWin32Error),
            GetExtra(aEvent, "win32_error"));
}

TEST_F(SandboxBrokerLaunchTelemetry, FailuresRecordedOncePerCombination) {
  // The per-process dedupe state isn't reset, so repeats (for example with
  // --gtest_repeat) find these combinations already recorded.
  static bool sHasRun = false;
  bool isRepeat = sHasRun;
  sHasRun = true;

  wchar_t systemDir[MAX_PATH];
  UINT len = ::GetSystemDirectoryW(systemDir, MAX_PATH);
  ASSERT_TRUE(len > 0 && len < MAX_PATH);
  std::wstring missingFile =
      std::wstring(systemDir) + L"\\sandbox_test_does_not_exist.exe";

  LaunchExpectingFailure(missingFile.c_str());
  LaunchExpectingFailure(missingFile.c_str());
  LaunchExpectingFailure(LR"(C:\sandbox_test_does_not_exist\firefox.exe)");

  EXPECT_EQ(3, GetCount(glean::sandbox::launch_attempts));
  EXPECT_EQ(3, GetCount(glean::sandbox::launch_failures));

  auto events = glean::sandbox::launch_failed.TestGetValue().unwrap();
  if (isRepeat) {
    EXPECT_TRUE(events.isNothing());
    return;
  }
  ASSERT_TRUE(events.isSome());
  ASSERT_EQ(2u, events->Length());
  ExpectFailedCreateProcessEvent(events->ElementAt(0), ERROR_FILE_NOT_FOUND);
  ExpectFailedCreateProcessEvent(events->ElementAt(1), ERROR_PATH_NOT_FOUND);
}

}  // namespace
}  // namespace mozilla
