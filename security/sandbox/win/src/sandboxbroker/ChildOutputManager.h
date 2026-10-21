/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_CHILDOUTPUTMANAGER_H_
#define SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_CHILDOUTPUTMANAGER_H_

#include <windows.h>

#include "mozilla/UniquePtrExtensions.h"

namespace sandbox {
class TargetPolicy;
}  // namespace sandbox

namespace mozilla {

/**
 * Creates the stdout and stderr handles for sandboxed child processes, which
 * are duplicates of the parent's std handles that only allow writing to pipes
 * and appending to files. Console handles can't be inherited by sandboxed
 * child processes, so they are given the write end of a shared pipe instead
 * and a reader thread copies everything written to it onto the console.
 */
class ChildOutputManager {
 public:
  /**
   * Creates the child handles from the parent's std handles. Must be called
   * once on the main thread before any sandboxed child is launched.
   */
  static void Initialize();

  /**
   * Sets the policy's stdout and stderr handles to any child handles that
   * were created. aPolicy is not retained.
   */
  static void SetChildStdHandles(sandbox::TargetPolicy& aPolicy);

  ChildOutputManager(const ChildOutputManager&) = delete;
  ChildOutputManager& operator=(const ChildOutputManager&) = delete;

 protected:
  /**
   * Creates the child handles, starting the relay if either of the parent's
   * std handles is a console, preferring stderr as the console to write to.
   */
  ChildOutputManager(HANDLE aStdout, HANDLE aStderr, bool aStdoutIsConsole,
                     bool aStderrIsConsole);

  /**
   * Closes the child handles and waits for the reader thread to finish, so
   * all other handles to the write end of the pipe must have been closed.
   */
  ~ChildOutputManager();

  static bool IsConsoleHandle(HANDLE aHandle);

  bool IsRelaying() const { return !!mReaderThread; }
  HANDLE ChildStdout() const { return mChildStdout.get(); }
  HANDLE ChildStderr() const { return mChildStderr.get(); }

 private:
  UniqueFileHandle StartRelay(HANDLE aConsoleHandle);
  static DWORD WINAPI ReaderThreadProc(LPVOID aManager);

  HANDLE mConsoleHandle = nullptr;
  UniqueFileHandle mPipeRead;
  UniqueFileHandle mReaderThread;
  // Inheritable handles for children, which may refer to the same object.
  UniqueFileHandle mChildStdout;
  UniqueFileHandle mChildStderr;
};

}  // namespace mozilla

#endif  // SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_CHILDOUTPUTMANAGER_H_
