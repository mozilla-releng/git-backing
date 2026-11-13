/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.flow

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import mozilla.components.support.test.robolectric.testContext
import org.junit.Rule
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class TabGroupFlowTelemetryMiddlewareTest {
    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private val store =
        TabGroupFlowStore(
            initialState = TabGroupFlowState(entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tabId = "123")),
            middlewares = listOf(TabGroupFlowTelemetryMiddleware()),
        )

    @Test
    fun `WHEN moving the last tab and deleting its group is confirmed THEN a tab added to group event is recorded`() {
        store.dispatch(
            TabGroupAction.MoveTabAndDeleteGroupConfirmed(
                sourceGroupId = "source",
                destinationGroupId = "destination",
                destinationGroupTitle = "Work",
            )
        )

        val event = TabsTray.tabAddedToGroup.testGetValue()!!.single()
        assertEquals("1", event.extra?.get("tab_count"))
    }

    @Test
    fun `WHEN moving the last tab and deleting its group is requested and canceled THEN no tab added to group event is recorded`() {
        store.dispatch(
            TabGroupAction.MoveTabAndDeleteGroupConfirmationRequested(
                sourceGroupId = "source",
                destinationGroupId = "destination",
                sourceGroupTitle = "Errands",
                destinationGroupTitle = "Work",
            )
        )
        store.dispatch(TabGroupAction.MoveTabAndDeleteGroupCanceled)

        assertNull(TabsTray.tabAddedToGroup.testGetValue())
    }
}
