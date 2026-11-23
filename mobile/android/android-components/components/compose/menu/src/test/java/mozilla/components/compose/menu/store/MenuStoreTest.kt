/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.store

import mozilla.components.lib.state.Middleware
import org.junit.Assert.assertEquals
import org.junit.Test

class MenuStoreTest {
    @Test
    fun `WHEN an impression is dispatched THEN record it and pass it on`() {
        val events = mutableListOf<MenuAction>()
        val store = MenuStore(middleware = recording(events))

        store.dispatch(FirstMenuImpression)

        assertEquals(setOf(FIRST_ID), store.state.recordedImpressions)
        assertEquals(listOf<MenuAction>(FirstMenuImpression), events.filterIsInstance<MenuImpressionEvent>())
    }

    @Test
    fun `WHEN the same impression is dispatched again THEN drop it before it reaches other middleware`() {
        val events = mutableListOf<MenuAction>()
        val store = MenuStore(middleware = recording(events))

        store.dispatch(FirstMenuImpression)
        store.dispatch(FirstMenuImpression)

        assertEquals(listOf<MenuAction>(FirstMenuImpression), events.filterIsInstance<MenuImpressionEvent>())
    }

    @Test
    fun `WHEN different impressions are dispatched THEN record and pass on each of them`() {
        val events = mutableListOf<MenuAction>()
        val store = MenuStore(middleware = recording(events))

        store.dispatch(FirstMenuImpression)
        store.dispatch(SecondMenuImpression)

        assertEquals(setOf(FIRST_ID, SECOND_ID), store.state.recordedImpressions)
        assertEquals(
            listOf<MenuAction>(FirstMenuImpression, SecondMenuImpression),
            events.filterIsInstance<MenuImpressionEvent>(),
        )
    }

    @Test
    fun `WHEN a non-impression event is dispatched twice THEN pass on both without recording an impression`() {
        val events = mutableListOf<MenuAction>()
        val store = MenuStore(middleware = recording(events))

        store.dispatch(OtherEvent)
        store.dispatch(OtherEvent)

        assertEquals(emptySet<String>(), store.state.recordedImpressions)
        assertEquals(listOf<MenuAction>(OtherEvent, OtherEvent), events.filterIsInstance<MenuEvent>())
    }

    private fun recording(events: MutableList<MenuAction>): List<Middleware<MenuState, MenuAction>> =
        listOf { _, next, action ->
            events += action
            next(action)
        }

    private data object FirstMenuImpression : MenuImpressionEvent {
        override val impressionId = FIRST_ID
    }

    private data object SecondMenuImpression : MenuImpressionEvent {
        override val impressionId = SECOND_ID
    }

    private data object OtherEvent : MenuEvent

    private companion object {
        const val FIRST_ID = "first"
        const val SECOND_ID = "second"
    }
}
