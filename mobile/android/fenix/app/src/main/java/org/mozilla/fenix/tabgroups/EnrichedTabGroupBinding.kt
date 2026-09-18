/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabdata.coordinator.TabDataCoordinator
import mozilla.components.feature.tabdata.coordinator.data.TabGroup as CoordinatorTabGroup
import mozilla.components.support.base.feature.LifecycleAwareFeature
import org.mozilla.fenix.tabstray.data.TabGroupTheme.Companion.toTabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem

/**
 * Helper class that produces enriched tab groups with the fields that are missing from the [TabDataCoordinator] but
 * needed as a [TabsTrayItem]
 *
 * @param browserStore Used to listen for tab updates
 * @param tabDataCoordinator Used to listen for group updates
 * @param onTabGroupsUpdated Called when enriched tab groups have been updated
 * @param computationDispatcher The [CoroutineDispatcher] on which computations occur
 * @param mainDispatcher The [CoroutineDispatcher] on which observations occur
 */
class EnrichedTabGroupBinding(
    private val browserStore: BrowserStore,
    private val tabDataCoordinator: TabDataCoordinator,
    private val onTabGroupsUpdated: (List<TabsTrayItem.TabGroup>) -> Unit,
    private val computationDispatcher: CoroutineDispatcher = Dispatchers.Default,
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
        val tabItems =
            browserStore.stateFlow
                .map { state -> state.tabs.associate { it.id to TabsTrayItem.Tab(tab = it) } }
                .distinctUntilChanged()

        combine(tabDataCoordinator.tabsSnapshot.filterNotNull(), tabItems) { snapshot, items ->
                snapshot.openTabGroups.map { it.toTabsTrayItem(tabItems = items) }
            }
            .distinctUntilChanged()
            .flowOn(computationDispatcher)
            .collect(onTabGroupsUpdated)
    }
}

internal fun CoordinatorTabGroup.toTabsTrayItem(tabItems: Map<String, TabsTrayItem.Tab>): TabsTrayItem.TabGroup =
    TabsTrayItem.TabGroup(
        id = id,
        title = title,
        theme = theme.toTabGroupTheme(),
        closed = closed,
        lastModified = lastModified,
        tabs = tabIds.mapNotNull { tabItems[it] },
    )
