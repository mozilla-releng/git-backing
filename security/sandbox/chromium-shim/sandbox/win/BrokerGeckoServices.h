/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SANDBOX_WIN_BROKERGECKOSERVICES_H_
#define SANDBOX_WIN_BROKERGECKOSERVICES_H_

#include <string_view>

namespace mozilla::sandboxing {

// Called by the broker, on any thread, for requests matched by rules added with
// sandbox::TargetConfig::AllowFontFileAccess. aNtPath is the path as sent by
// the target. Returns true if read-only access should be allowed.
using ValidateFontFileFn = bool (*)(std::wstring_view aNtPath);

// Services provided by Gecko to the sandbox broker code. These must be set
// before any sandboxed process is launched.
struct BrokerGeckoServices {
  ValidateFontFileFn validateFontFile = nullptr;
};

using SetBrokerGeckoServicesCb = void (*)(const BrokerGeckoServices&);

void SetBrokerGeckoServices(const BrokerGeckoServices& aServices);

}  // namespace mozilla::sandboxing

#endif  // SANDBOX_WIN_BROKERGECKOSERVICES_H_
