/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SECURITY_SANDBOX_WIN_SANDBOXBROKERDATA_H_
#define SECURITY_SANDBOX_WIN_SANDBOXBROKERDATA_H_

#include "mozilla/sandboxing/BrokerGeckoServices.h"

namespace sandbox {
class BrokerServices;
}

namespace mozilla::sandboxing {

/**
 * Data passed from the executable to xul to initialize SandboxBroker.
 */
struct SandboxBrokerData {
  /* Chromium sandbox BrokerServices. */
  sandbox::BrokerServices* brokerServices = nullptr;
  /* Callback to set BrokerGeckoServices in the chromium sandbox code. */
  SetBrokerGeckoServicesCb setBrokerGeckoServices = nullptr;
};

}  // namespace mozilla::sandboxing

#endif  // SECURITY_SANDBOX_WIN_SANDBOXBROKERDATA_H_
