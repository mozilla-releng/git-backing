/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ChildOutputManager.h"

#include "mozilla/Assertions.h"
#include "nsThreadUtils.h"
#include "sandbox/win/src/sandbox_policy.h"

namespace mozilla {

static constexpr DWORD kPipeBufferSize = 64 * 1024;
static constexpr DWORD kReadBufferSize = 4 * 1024;
static constexpr DWORD kThreadStackSize = 64 * 1024;

// Without FILE_WRITE_ATTRIBUTES children can't change a pipe's mode.
static constexpr DWORD kChildPipeAccess =
    FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE;

// Without FILE_WRITE_DATA children can only append to files.
static constexpr DWORD kChildFileAccess =
    FILE_APPEND_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE;

// Intentionally leaked, as the child handles are needed and the reader thread
// runs until the process exits.
static ChildOutputManager* sInstance = nullptr;

static HANDLE DuplicateForChild(HANDLE aHandle, DWORD aAccess) {
  HANDLE childHandle;
  if (!::DuplicateHandle(::GetCurrentProcess(), aHandle, ::GetCurrentProcess(),
                         &childHandle, aAccess, TRUE, 0)) {
    return nullptr;
  }
  return childHandle;
}

static HANDLE CreateChildHandle(HANDLE aHandle) {
  if (!aHandle || aHandle == INVALID_HANDLE_VALUE) {
    return nullptr;
  }

  switch (::GetFileType(aHandle)) {
    case FILE_TYPE_DISK:
      return DuplicateForChild(aHandle, kChildFileAccess);
    case FILE_TYPE_PIPE:
      return DuplicateForChild(aHandle, kChildPipeAccess);
    default:
      return nullptr;
  }
}

static void CloseIfSet(HANDLE aHandle) {
  if (aHandle) {
    ::CloseHandle(aHandle);
  }
}

/* static */
void ChildOutputManager::Initialize() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!sInstance);

  HANDLE stdoutHandle = ::GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE stderrHandle = ::GetStdHandle(STD_ERROR_HANDLE);
  sInstance = new ChildOutputManager(stdoutHandle, stderrHandle,
                                     IsConsoleHandle(stdoutHandle),
                                     IsConsoleHandle(stderrHandle));
}

/* static */
void ChildOutputManager::SetChildStdHandles(sandbox::TargetPolicy& aPolicy) {
  if (!sInstance) {
    return;
  }

  if (sInstance->mChildStdout) {
    aPolicy.SetStdoutHandle(sInstance->mChildStdout);
  }
  if (sInstance->mChildStderr) {
    aPolicy.SetStderrHandle(sInstance->mChildStderr);
  }
}

ChildOutputManager::ChildOutputManager(HANDLE aStdout, HANDLE aStderr,
                                       bool aStdoutIsConsole,
                                       bool aStderrIsConsole) {
  HANDLE relayHandle = nullptr;
  if (aStderrIsConsole) {
    relayHandle = StartRelay(aStderr);
  } else if (aStdoutIsConsole) {
    relayHandle = StartRelay(aStdout);
  }

  mChildStdout = aStdoutIsConsole ? relayHandle : CreateChildHandle(aStdout);
  if (aStderr == aStdout || (aStdoutIsConsole && aStderrIsConsole)) {
    mChildStderr = mChildStdout;
  } else {
    mChildStderr = aStderrIsConsole ? relayHandle : CreateChildHandle(aStderr);
  }
}

ChildOutputManager::~ChildOutputManager() {
  if (mChildStderr != mChildStdout) {
    CloseIfSet(mChildStderr);
  }
  CloseIfSet(mChildStdout);
  if (mReaderThread) {
    ::WaitForSingleObject(mReaderThread, INFINITE);
    ::CloseHandle(mReaderThread);
  }
  CloseIfSet(mPipeRead);
}

/* static */
bool ChildOutputManager::IsConsoleHandle(HANDLE aHandle) {
  DWORD mode;
  return aHandle && aHandle != INVALID_HANDLE_VALUE &&
         ::GetConsoleMode(aHandle, &mode);
}

HANDLE ChildOutputManager::StartRelay(HANDLE aConsoleHandle) {
  HANDLE pipeRead;
  HANDLE pipeWrite;
  if (!::CreatePipe(&pipeRead, &pipeWrite, nullptr, kPipeBufferSize)) {
    return nullptr;
  }

  // Only the child handle is kept, so that the parent has no handle that
  // allows more access.
  HANDLE childHandle = DuplicateForChild(pipeWrite, kChildPipeAccess);
  ::CloseHandle(pipeWrite);
  if (!childHandle) {
    ::CloseHandle(pipeRead);
    return nullptr;
  }

  mConsoleHandle = aConsoleHandle;
  mPipeRead = pipeRead;
  mReaderThread =
      ::CreateThread(nullptr, kThreadStackSize, ReaderThreadProc, this,
                     STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
  if (!mReaderThread) {
    ::CloseHandle(childHandle);
    ::CloseHandle(mPipeRead);
    mPipeRead = nullptr;
    mConsoleHandle = nullptr;
    return nullptr;
  }

  ::SetThreadDescription(mReaderThread, L"ChildOutputRelay");
  return childHandle;
}

/* static */
DWORD WINAPI ChildOutputManager::ReaderThreadProc(LPVOID aManager) {
  auto* manager = static_cast<ChildOutputManager*>(aManager);
  char buffer[kReadBufferSize];
  DWORD bytesRead;
  while (::ReadFile(manager->mPipeRead, buffer, sizeof(buffer), &bytesRead,
                    nullptr)) {
    DWORD offset = 0;
    while (offset < bytesRead) {
      DWORD bytesWritten;
      if (!::WriteFile(manager->mConsoleHandle, buffer + offset,
                       bytesRead - offset, &bytesWritten, nullptr) ||
          bytesWritten == 0) {
        break;
      }
      offset += bytesWritten;
    }
  }
  return 0;
}

}  // namespace mozilla
