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

static UniqueFileHandle DuplicateForChild(HANDLE aHandle, DWORD aAccess,
                                          DWORD aOptions = 0) {
  HANDLE childHandle;
  if (!::DuplicateHandle(::GetCurrentProcess(), aHandle, ::GetCurrentProcess(),
                         &childHandle, aAccess, TRUE, aOptions)) {
    return nullptr;
  }
  return UniqueFileHandle(childHandle);
}

static UniqueFileHandle CreateChildHandle(HANDLE aHandle) {
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
  MOZ_ASSERT(sInstance, "Initialize must be called before launching children.");
  if (!sInstance) {
    return;
  }

  if (sInstance->mChildStdout) {
    aPolicy.SetStdoutHandle(sInstance->mChildStdout.get());
  }
  if (sInstance->mChildStderr) {
    aPolicy.SetStderrHandle(sInstance->mChildStderr.get());
  }
}

ChildOutputManager::ChildOutputManager(HANDLE aStdout, HANDLE aStderr,
                                       bool aStdoutIsConsole,
                                       bool aStderrIsConsole) {
  if (aStderrIsConsole) {
    mChildStderr = StartRelay(aStderr);
    mChildStdout = aStdoutIsConsole ? DuplicateForChild(mChildStderr.get(), 0,
                                                        DUPLICATE_SAME_ACCESS)
                                    : CreateChildHandle(aStdout);
  } else {
    mChildStderr = CreateChildHandle(aStderr);
    mChildStdout =
        aStdoutIsConsole ? StartRelay(aStdout) : CreateChildHandle(aStdout);
  }
}

ChildOutputManager::~ChildOutputManager() {
  mChildStdout = nullptr;
  mChildStderr = nullptr;
  if (mReaderThread) {
    ::WaitForSingleObject(mReaderThread.get(), INFINITE);
  }
}

/* static */
bool ChildOutputManager::IsConsoleHandle(HANDLE aHandle) {
  DWORD mode;
  return aHandle && aHandle != INVALID_HANDLE_VALUE &&
         ::GetConsoleMode(aHandle, &mode);
}

UniqueFileHandle ChildOutputManager::StartRelay(HANDLE aConsoleHandle) {
  HANDLE pipeRead;
  HANDLE pipeWrite;
  if (!::CreatePipe(&pipeRead, &pipeWrite, nullptr, kPipeBufferSize)) {
    return nullptr;
  }
  UniqueFileHandle readHandle(pipeRead);

  // Only the child handle is kept, so that the parent has no handle that
  // allows more access.
  UniqueFileHandle childHandle = DuplicateForChild(pipeWrite, kChildPipeAccess);
  ::CloseHandle(pipeWrite);
  if (!childHandle) {
    return nullptr;
  }

  mConsoleHandle = aConsoleHandle;
  mPipeRead = std::move(readHandle);
  mReaderThread.reset(
      ::CreateThread(nullptr, kThreadStackSize, ReaderThreadProc, this,
                     STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr));
  if (!mReaderThread) {
    mPipeRead = nullptr;
    mConsoleHandle = nullptr;
    return nullptr;
  }

  ::SetThreadDescription(mReaderThread.get(), L"ChildOutputRelay");
  return childHandle;
}

/* static */
DWORD WINAPI ChildOutputManager::ReaderThreadProc(LPVOID aManager) {
  auto* manager = static_cast<ChildOutputManager*>(aManager);
  char buffer[kReadBufferSize];
  DWORD bytesRead;
  while (::ReadFile(manager->mPipeRead.get(), buffer, sizeof(buffer),
                    &bytesRead, nullptr)) {
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
