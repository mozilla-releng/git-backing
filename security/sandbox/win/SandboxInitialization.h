/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_sandboxing_SandboxInitialization_h
#define mozilla_sandboxing_SandboxInitialization_h

#include "mozilla/sandboxing/SandboxBrokerData.h"

namespace sandbox {
class TargetServices;
}  // namespace sandbox

// Things that use this file will probably want access to the IsSandboxedProcess
// function defined in one of the Chromium sandbox cc files.
extern "C" bool IsSandboxedProcess();

namespace mozilla {
// Note the Chromium code just uses a bare sandbox namespace, which makes using
// sandbox for our namespace painful.
namespace sandboxing {

/**
 * Initializes (if required) and returns the Chromium sandbox TargetServices.
 *
 * @return the TargetServices or null if the creation or initialization failed.
 */
sandbox::TargetServices* GetInitializedTargetServices();

/**
 * Lowers the permissions on the process sandbox.
 * Provided because the GMP sandbox needs to be lowered from the executable.
 */
void LowerSandbox();

/**
 * Initializes (if required) the Chromium sandbox BrokerServices and returns
 * the data needed to initialize SandboxBroker in xul.
 *
 * @return the SandboxBrokerData, with null brokerServices if the creation or
 *         initialization failed.
 */
SandboxBrokerData GetInitializedSandboxBrokerData();

/**
 * Apply mitigations for parent processes.
 */
void ApplyParentProcessMitigations();

}  // namespace sandboxing
}  // namespace mozilla

#endif  // mozilla_sandboxing_SandboxInitialization_h
