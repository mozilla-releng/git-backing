/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.components.fake

import kotlinx.coroutines.flow.MutableStateFlow
import mozilla.components.feature.tabdata.coordinator.TabDataCoordinator
import mozilla.components.feature.tabdata.coordinator.data.TabsSnapshot

/** Fake [TabDataCoordinator] for unit testing */
class FakeTabDataCoordinator(snapshot: TabsSnapshot? = null) : TabDataCoordinator {
    override val tabsSnapshot = MutableStateFlow(snapshot)

    override fun initialize() = Unit
}
