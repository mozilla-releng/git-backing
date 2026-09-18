/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabdata.coordinator.TabDataCoordinator
import mozilla.components.support.base.feature.LifecycleAwareFeature

/**
 * Helper class that observes [TabDataCoordinator] data in order to communicate to the menu whether the current open
 * (selected) tab is in a group. This allows the menu item to provide contextually appropriate tab group options.
 *
 * Note that there are two 'selected tabs' in the menu context. browserMenuState.selectedTab may be either The
 * [BrowserStore] selected tab, which is consumed here though [TabDataCoordinator], or a custom tab. This binding only
 * observes the [TabDataCoordinator] state and not used in the custom tab menu.
 *
 * @param tabDataCoordinator Tells us whether the currently selected tab is in a group.
 * @param onDisplayGroupOptionsUpdated Called to update the menu state.
 * @param mainDispatcher Dispatcher that the observations occur on.
 */
class TabGroupMenuBinding(
    private val tabDataCoordinator: TabDataCoordinator,
    private val onDisplayGroupOptionsUpdated: (Boolean) -> Unit,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : LifecycleAwareFeature {
    private var scope: CoroutineScope? = null

    override fun start() {
        scope = CoroutineScope(mainDispatcher).also { it.launch { observe() } }
    }

    override fun stop() {
        scope?.cancel()
        scope = null
    }

    private suspend fun observe() {
        tabDataCoordinator.tabsSnapshot
            .filterNotNull()
            .map { it.selectedTab?.tabGroupId != null }
            .distinctUntilChanged()
            .collect(onDisplayGroupOptionsUpdated)
    }
}
