/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlinx.coroutines.test.StandardTestDispatcher
import mozilla.components.feature.tabdata.coordinator.TabDataCoordinator
import mozilla.components.feature.tabdata.coordinator.data.Tab
import mozilla.components.feature.tabdata.coordinator.data.TabsSnapshot
import org.junit.Test
import org.mozilla.fenix.components.fake.FakeTabDataCoordinator

class TabGroupMenuBindingTest {
    private val testDispatcher = StandardTestDispatcher()

    @Test
    fun `WHEN tab is in a group and not a custom tab THEN tab group options are displayed`() {
        val tabDataCoordinator =
            FakeTabDataCoordinator(
                TabsSnapshot(
                    selectedTab =
                        Tab(
                            id = "1",
                            url = "mozilla.org",
                            title = "Title",
                            private = false,
                            inactive = false,
                            tabGroupId = "2",
                        )
                )
            )
        startBinding(tabDataCoordinator = tabDataCoordinator) { result ->
            assertTrue(result)
        }
    }

    @Test
    fun `WHEN tab is not in a group and not a custom tab THEN tab group options are not displayed`() {

        val tabDataCoordinator =
            FakeTabDataCoordinator(
                TabsSnapshot(
                    selectedTab =
                        Tab(
                            id = "1",
                            url = "mozilla.org",
                            title = "Title",
                            private = false,
                            inactive = false,
                            tabGroupId = null,
                        )
                )
            )
        startBinding(tabDataCoordinator = tabDataCoordinator) { result ->
            assertFalse(result)
        }
    }

    private fun startBinding(
        tabDataCoordinator: TabDataCoordinator,
        onDisplayGroupOptionsUpdated: (Boolean) -> Unit,
    ) {
        val tabGroupMenuBinding =
            TabGroupMenuBinding(
                tabDataCoordinator = tabDataCoordinator,
                onDisplayGroupOptionsUpdated = onDisplayGroupOptionsUpdated,
                mainDispatcher = testDispatcher,
            )
        tabGroupMenuBinding.start()
        testDispatcher.scheduler.advanceUntilIdle()
    }
}
