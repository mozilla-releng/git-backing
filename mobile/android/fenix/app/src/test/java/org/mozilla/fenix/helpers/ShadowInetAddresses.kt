/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.helpers

import android.net.InetAddresses
import android.util.Patterns
import org.robolectric.annotation.Implementation
import org.robolectric.annotation.Implements

/**
 * Robolectric default implementation of [InetAddresses] resolves hostnames via DNS and returns true for any that
 * resolve. This shadow is used to override that behavior and return true for any IP address.
 */
@Implements(InetAddresses::class)
// Robolectric instantiates shadows, so this has to stay a class.
@Suppress("UtilityClassWithPublicConstructor")
class ShadowInetAddresses {
    companion object {
        @Implementation
        @JvmStatic
        @Suppress("DEPRECATION")
        fun isNumericAddress(address: String): Boolean {
            return Patterns.IP_ADDRESS.matcher(address).matches() || address.contains(":")
        }
    }
}
