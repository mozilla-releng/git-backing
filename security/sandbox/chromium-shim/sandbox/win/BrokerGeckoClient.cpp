/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BrokerGeckoClient.h"

namespace mozilla::sandboxing {

static BrokerGeckoServices sBrokerGeckoServices;

void SetBrokerGeckoServices(const BrokerGeckoServices& aServices) {
  sBrokerGeckoServices = aServices;
}

bool GeckoValidateFontFile(std::wstring_view aNtPath) {
  return sBrokerGeckoServices.validateFontFile &&
         sBrokerGeckoServices.validateFontFile(aNtPath);
}

}  // namespace mozilla::sandboxing
