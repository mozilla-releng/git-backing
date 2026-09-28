/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix

import androidx.annotation.WorkerThread
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoSession

@WorkerThread
fun lintProbeOpenSessionOffMainThread(session: GeckoSession, runtime: GeckoRuntime) {
    session.open(runtime)
}
