/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ChildConsoleRelay.h"

#include "mozilla/Assertions.h"
#include "nsThreadUtils.h"
#include "sandbox/win/src/sandbox_policy.h"

namespace mozilla {

static HANDLE sConsoleHandle = nullptr;
static HANDLE sPipeRead = nullptr;
static HANDLE sPipeWrite = nullptr;

static constexpr DWORD kPipeBufferSize = 64 * 1024;
static constexpr DWORD kReadBufferSize = 4 * 1024;
static constexpr DWORD kThreadStackSize = 64 * 1024;

static DWORD WINAPI RelayThreadProc(LPVOID) {
  char buffer[kReadBufferSize];
  DWORD bytesRead;
  while (::ReadFile(sPipeRead, buffer, sizeof(buffer), &bytesRead, nullptr)) {
    DWORD offset = 0;
    while (offset < bytesRead) {
      DWORD bytesWritten;
      if (!::WriteFile(sConsoleHandle, buffer + offset, bytesRead - offset,
                       &bytesWritten, nullptr) ||
          bytesWritten == 0) {
        break;
      }
      offset += bytesWritten;
    }
  }
  return 0;
}

static bool IsConsoleHandle(HANDLE aHandle) {
  return aHandle && aHandle != INVALID_HANDLE_VALUE &&
         ::GetFileType(aHandle) == FILE_TYPE_CHAR;
}

/* static */
void ChildConsoleRelay::Initialize() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!sPipeWrite);

  HANDLE consoleHandle = ::GetStdHandle(STD_ERROR_HANDLE);
  if (!IsConsoleHandle(consoleHandle)) {
    consoleHandle = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (!IsConsoleHandle(consoleHandle)) {
      return;
    }
  }

  HANDLE pipeRead;
  HANDLE pipeWrite;
  if (!::CreatePipe(&pipeRead, &pipeWrite, nullptr, kPipeBufferSize)) {
    return;
  }

  sConsoleHandle = consoleHandle;
  sPipeRead = pipeRead;
  HANDLE thread =
      ::CreateThread(nullptr, kThreadStackSize, RelayThreadProc, nullptr,
                     STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
  if (!thread) {
    ::CloseHandle(pipeRead);
    ::CloseHandle(pipeWrite);
    sConsoleHandle = nullptr;
    sPipeRead = nullptr;
    return;
  }

  ::SetThreadDescription(thread, L"ChildConsoleRelay");
  ::CloseHandle(thread);
  sPipeWrite = pipeWrite;
}

ChildConsoleRelay::ChildConsoleRelay(sandbox::TargetPolicy& aPolicy) {
  HANDLE stdoutHandle = ::GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE stderrHandle = ::GetStdHandle(STD_ERROR_HANDLE);
  if (sPipeWrite) {
    bool stdoutIsConsole = IsConsoleHandle(stdoutHandle);
    bool stderrIsConsole = IsConsoleHandle(stderrHandle);
    if (stdoutIsConsole || stderrIsConsole) {
      HANDLE inheritable;
      if (::DuplicateHandle(::GetCurrentProcess(), sPipeWrite,
                            ::GetCurrentProcess(), &inheritable, 0, TRUE,
                            DUPLICATE_SAME_ACCESS)) {
        mInheritableWriteHandle.reset(inheritable);
        if (stdoutIsConsole) {
          stdoutHandle = inheritable;
        }
        if (stderrIsConsole) {
          stderrHandle = inheritable;
        }
      }
    }
  }

  aPolicy.SetStdoutHandle(stdoutHandle);
  aPolicy.SetStderrHandle(stderrHandle);
}

}  // namespace mozilla
