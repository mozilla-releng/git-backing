/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#define MOZ_USE_LAUNCHER_ERROR

#include <windows.h>

#include <stdio.h>

#include "mozilla/WinHeaderOnlyUtils.h"
#include "nsWindowsHelpers.h"
#include "ProcThreadAttributes.h"

static bool Check(bool aCondition, const char* aMsg) {
  if (!aCondition) {
    printf("TEST-FAILED | TestProcThreadAttributes | %s\n", aMsg);
  }
  return aCondition;
}

static bool IsInheritable(HANDLE aHandle) {
  DWORD flags = 0;
  return ::GetHandleInformation(aHandle, &flags) &&
         (flags & HANDLE_FLAG_INHERIT);
}

static bool TestRejectsInvalidHandles() {
  mozilla::ProcThreadAttributes attrs;
  return Check(!attrs.AddInheritableHandle(nullptr),
               "A null handle must be rejected.") &&
         Check(!attrs.AddInheritableHandle(INVALID_HANDLE_VALUE),
               "INVALID_HANDLE_VALUE must be rejected.") &&
         Check(!attrs.HasInheritableHandles(),
               "No handles should have been added.");
}

static bool TestAcceptsNonConsoleHandles() {
  wchar_t tempDir[MAX_PATH];
  wchar_t tempFile[MAX_PATH];
  if (!Check(::GetTempPathW(MAX_PATH, tempDir) &&
                 ::GetTempFileNameW(tempDir, L"pta", 0, tempFile),
             "Unable to get a temporary file name.")) {
    return false;
  }
  nsAutoHandle file(::CreateFileW(tempFile, GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE,
                                  nullptr));

  HANDLE pipeRead;
  HANDLE pipeWrite;
  if (!Check(::CreatePipe(&pipeRead, &pipeWrite, nullptr, 0),
             "Unable to create a pipe.")) {
    return false;
  }
  nsAutoHandle pipeReadHolder(pipeRead);
  nsAutoHandle pipeWriteHolder(pipeWrite);

  nsAutoHandle nul(::CreateFileW(L"NUL", GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, 0, nullptr));

  if (!Check(file.get() != INVALID_HANDLE_VALUE,
             "Unable to create a temporary file.") ||
      !Check(nul.get() != INVALID_HANDLE_VALUE, "Unable to open NUL.")) {
    return false;
  }

  mozilla::ProcThreadAttributes attrs;
  return Check(attrs.AddInheritableHandle(file),
               "A file handle must be accepted.") &&
         Check(IsInheritable(file),
               "A file handle must be made inheritable.") &&
         Check(attrs.AddInheritableHandle(pipeWrite),
               "A pipe handle must be accepted.") &&
         Check(IsInheritable(pipeWrite),
               "A pipe handle must be made inheritable.") &&
         Check(attrs.AddInheritableHandle(nul),
               "A NUL handle must be accepted.") &&
         Check(IsInheritable(nul), "A NUL handle must be made inheritable.") &&
         Check(attrs.HasInheritableHandles(),
               "Handles should have been added.");
}

static bool TestRejectsConsoleHandles() {
  if (!::GetConsoleWindow() && !::AllocConsole()) {
    printf("TEST-SKIPPED | TestProcThreadAttributes | No console available.\n");
    return true;
  }

  nsAutoHandle console(::CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                     FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                     0, nullptr));
  if (!Check(console.get() != INVALID_HANDLE_VALUE,
             "Unable to open the console.")) {
    return false;
  }

  mozilla::ProcThreadAttributes attrs;
  return Check(!attrs.AddInheritableHandle(console),
               "A console handle must be rejected.") &&
         Check(!IsInheritable(console),
               "A console handle must not be made inheritable.") &&
         Check(!attrs.HasInheritableHandles(),
               "No handles should have been added.");
}

static bool TestRejectedHandlesAreCleared() {
  HANDLE pipeRead;
  HANDLE pipeWrite;
  if (!Check(::CreatePipe(&pipeRead, &pipeWrite, nullptr, 0),
             "Unable to create a pipe.")) {
    return false;
  }
  nsAutoHandle pipeReadHolder(pipeRead);
  nsAutoHandle pipeWriteHolder(pipeWrite);

  HANDLE handles[] = {nullptr, pipeWrite, INVALID_HANDLE_VALUE};
  mozilla::ProcThreadAttributes attrs;
  return Check(!attrs.AddInheritableHandles(handles),
               "Adding handles that are rejected must fail.") &&
         Check(handles[0] == nullptr && handles[2] == nullptr,
               "Rejected handles must be cleared.") &&
         Check(handles[1] == pipeWrite,
               "Accepted handles must be left unchanged.") &&
         Check(attrs.HasInheritableHandles(),
               "The accepted handle should have been added.");
}

int main() {
  if (!TestRejectsInvalidHandles() || !TestAcceptsNonConsoleHandles() ||
      !TestRejectsConsoleHandles() || !TestRejectedHandlesAreCleared()) {
    return 1;
  }

  printf("TEST-PASS | TestProcThreadAttributes | All tests passed.\n");
  return 0;
}
