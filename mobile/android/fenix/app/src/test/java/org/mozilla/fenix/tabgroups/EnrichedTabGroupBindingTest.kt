/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlinx.coroutines.test.StandardTestDispatcher
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabdata.coordinator.data.TabGroup
import mozilla.components.feature.tabdata.coordinator.data.TabsSnapshot
import mozilla.components.feature.tabdata.coordinator.fakes.FakeTabDataCoordinator
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem

@RunWith(AndroidJUnit4::class)
class EnrichedTabGroupBindingTest {

    private val testDispatcher = StandardTestDispatcher()
    private val firstTab = createTab(id = "tab-1", url = "https://mozilla.org")
    private val secondTab = createTab(id = "tab-2", url = "https://example.com")
    private val updates = mutableListOf<List<TabsTrayItem.TabGroup>>()

    @Test
    fun `WHEN tab data is available THEN open groups are emitted with their browser tabs`() {
        val browserStore = BrowserStore(BrowserState(tabs = listOf(firstTab, secondTab)))
        val coordinator = FakeTabDataCoordinator(snapshotWith(storedGroup(tabIds = listOf(firstTab.id, secondTab.id))))

        startBinding(browserStore, coordinator)

        assertEquals(
            listOf(
                TabsTrayItem.TabGroup(
                    id = GROUP_ID,
                    title = GROUP_TITLE,
                    theme = TabGroupTheme.Pink,
                    closed = false,
                    lastModified = LAST_MODIFIED,
                    tabs = listOf(TabsTrayItem.Tab(tab = firstTab), TabsTrayItem.Tab(tab = secondTab)),
                )
            ),
            updates.last(),
        )
    }

    @Test
    fun `GIVEN a group references a tab that is not in the browser store WHEN enriching THEN the missing tab is dropped`() {
        val browserStore = BrowserStore(BrowserState(tabs = listOf(firstTab)))
        val coordinator = FakeTabDataCoordinator(snapshotWith(storedGroup(tabIds = listOf(firstTab.id, "missing"))))

        startBinding(browserStore, coordinator)

        assertEquals(listOf(TabsTrayItem.Tab(tab = firstTab)), updates.last().single().tabs)
    }

    @Test
    fun `GIVEN tab data is not loaded yet WHEN the binding starts THEN nothing is emitted`() {
        val browserStore = BrowserStore(BrowserState(tabs = listOf(firstTab)))
        val coordinator = FakeTabDataCoordinator(initialTabsSnapshot = null)

        startBinding(browserStore, coordinator)

        assertTrue(updates.isEmpty())
    }

    @Test
    fun `WHEN the tab groups change THEN the updated groups are emitted`() {
        val browserStore = BrowserStore(BrowserState(tabs = listOf(firstTab, secondTab)))
        val coordinator = FakeTabDataCoordinator(snapshotWith(storedGroup(tabIds = listOf(firstTab.id))))
        startBinding(browserStore, coordinator)

        coordinator.tabsSnapshot.value = snapshotWith(storedGroup(tabIds = listOf(firstTab.id, secondTab.id)))
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(
            listOf(TabsTrayItem.Tab(tab = firstTab), TabsTrayItem.Tab(tab = secondTab)),
            updates.last().single().tabs,
        )
    }

    @Test
    fun `WHEN a grouped tab is removed from the browser THEN it is removed from the emitted group`() {
        val browserStore = BrowserStore(BrowserState(tabs = listOf(firstTab, secondTab)))
        val coordinator = FakeTabDataCoordinator(snapshotWith(storedGroup(tabIds = listOf(firstTab.id, secondTab.id))))
        startBinding(browserStore, coordinator)

        browserStore.dispatch(TabListAction.RemoveTabAction(tabId = secondTab.id))
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(listOf(TabsTrayItem.Tab(tab = firstTab)), updates.last().single().tabs)
    }

    @Test
    fun `WHEN the binding is stopped THEN later changes are not emitted`() {
        val browserStore = BrowserStore(BrowserState(tabs = listOf(firstTab, secondTab)))
        val coordinator = FakeTabDataCoordinator(snapshotWith(storedGroup(tabIds = listOf(firstTab.id))))
        val binding = startBinding(browserStore, coordinator)
        val emissionsBeforeStop = updates.size

        binding.stop()
        coordinator.tabsSnapshot.value = snapshotWith(storedGroup(tabIds = listOf(firstTab.id, secondTab.id)))
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(emissionsBeforeStop, updates.size)
    }

    private fun startBinding(
        browserStore: BrowserStore,
        coordinator: FakeTabDataCoordinator,
    ) =
        EnrichedTabGroupBinding(
                browserStore = browserStore,
                tabDataCoordinator = coordinator,
                onTabGroupsUpdated = { updates.add(it) },
                computationDispatcher = testDispatcher,
                mainDispatcher = testDispatcher,
            )
            .also {
                it.start()
                testDispatcher.scheduler.advanceUntilIdle()
            }

    private fun storedGroup(tabIds: List<String>) =
        TabGroup(
            id = GROUP_ID,
            title = GROUP_TITLE,
            theme = TabGroupTheme.Pink.toStorageValue(),
            closed = false,
            lastModified = LAST_MODIFIED,
            tabIds = tabIds,
        )

    private fun snapshotWith(vararg openGroups: TabGroup) = TabsSnapshot(openTabGroups = openGroups.toList())

    private companion object {
        const val GROUP_ID = "group-1"
        const val GROUP_TITLE = "Work"
        const val LAST_MODIFIED = 5L
    }
}
