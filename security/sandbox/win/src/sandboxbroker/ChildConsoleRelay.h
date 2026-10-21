/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_CHILDCONSOLERELAY_H_
#define SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_CHILDCONSOLERELAY_H_

#include <windows.h>

#include "mozilla/Attributes.h"
#include "mozilla/UniquePtrExtensions.h"

namespace sandbox {
class TargetPolicy;
}  // namespace sandbox

namespace mozilla {

/**
 * Console handles can't be inherited by sandboxed child processes, so when the
 * parent's stdout or stderr is a console, children are given the write end of
 * a shared pipe instead and a reader thread copies everything written to it
 * onto the parent's console.
 */
class MOZ_RAII ChildConsoleRelay {
 public:
  /**
   * Creates the pipe and reader thread if either of the parent's std handles is
   * a console. Must be called once on the main thread before any sandboxed
   * child is launched.
   */
  static void Initialize();

  /**
   * Sets the policy's stdout and stderr handles, replacing any console handles
   * with an inheritable handle to the relay pipe. aPolicy is not retained.
   */
  explicit ChildConsoleRelay(sandbox::TargetPolicy& aPolicy);

  ChildConsoleRelay(const ChildConsoleRelay&) = delete;
  ChildConsoleRelay& operator=(const ChildConsoleRelay&) = delete;

  /**
   * Closes the inheritable handle, which should be done as soon as the child
   * has been spawned. Also happens on destruction.
   */
  void ReleaseInheritableHandle() { mInheritableWriteHandle = nullptr; }

 private:
  UniqueFileHandle mInheritableWriteHandle;
};

}  // namespace mozilla

#endif  // SECURITY_SANDBOX_WIN_SRC_SANDBOXBROKER_CHILDCONSOLERELAY_H_
