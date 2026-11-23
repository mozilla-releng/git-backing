/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.width
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.getUnclippedBoundsInRoot
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.StateRestorationTester
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onAllNodesWithContentDescription
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.height
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.menu.R as MozacMenuR
import mozilla.components.compose.menu.data.BannerMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuAttribution
import mozilla.components.compose.menu.data.MenuItemSummary
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuAction
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.store.MenuImpressionEvent
import mozilla.components.compose.menu.store.MenuState
import mozilla.components.compose.menu.store.MenuStore
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.lib.state.Middleware
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.ui.icons.R as iconsR
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MenuTest {
    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `GIVEN a short menu with a footer THEN wrap content instead of filling the maximum height`() {
        setMenu(listOf(row("Item"), stickyGroup("Footer")))

        composeTestRule.onNodeWithText("Item").assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription("Footer").assertIsDisplayed()
        val menuBounds = composeTestRule.onNodeWithTag("menu").getUnclippedBoundsInRoot()
        val listBounds = composeTestRule.onNode(hasScrollAction()).getUnclippedBoundsInRoot()
        val footerBounds = composeTestRule.onNodeWithContentDescription("Footer").getUnclippedBoundsInRoot()
        assertTrue(menuBounds.height < 300.dp)
        assertTrue(listBounds.bottom <= footerBounds.top)
    }

    @Test
    fun `GIVEN a long menu WHEN scrolling to the end THEN keep header and footer visible without covering content`() {
        setMenu(listOf(stickyGroup("Header")) + List(20) { row("Item $it") } + stickyGroup("Footer"))
        val headerBefore = composeTestRule.onNodeWithContentDescription("Header").getUnclippedBoundsInRoot()
        val footerBefore = composeTestRule.onNodeWithContentDescription("Footer").getUnclippedBoundsInRoot()

        composeTestRule.onNode(hasScrollAction()).performScrollToNode(hasText("Item 19"))

        composeTestRule.onNodeWithContentDescription("Header").assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription("Footer").assertIsDisplayed()
        composeTestRule.onNodeWithText("Item 19").assertIsDisplayed()
        val headerAfter = composeTestRule.onNodeWithContentDescription("Header").getUnclippedBoundsInRoot()
        val footerAfter = composeTestRule.onNodeWithContentDescription("Footer").getUnclippedBoundsInRoot()
        val listBounds = composeTestRule.onNode(hasScrollAction()).getUnclippedBoundsInRoot()
        val lastItemBounds = composeTestRule.onNodeWithText("Item 19").getUnclippedBoundsInRoot()
        assertEquals(headerBefore.top, headerAfter.top)
        assertEquals(footerBefore.bottom, footerAfter.bottom)
        assertTrue(listBounds.bottom <= footerAfter.top)
        assertTrue(lastItemBounds.bottom <= listBounds.bottom)
    }

    @Test
    fun `WHEN a footer is added and removed THEN restore the original menu height`() {
        val groups = listOf(row("Item"))
        val store = setMenu(groups)
        val originalHeight = composeTestRule.onNodeWithTag("menu").getUnclippedBoundsInRoot().height

        composeTestRule.runOnIdle { store.dispatch(MenuAction.Update(groups + stickyGroup("Footer"))) }

        composeTestRule.onNodeWithContentDescription("Footer").assertIsDisplayed()
        assertTrue(composeTestRule.onNodeWithTag("menu").getUnclippedBoundsInRoot().height > originalHeight)

        composeTestRule.runOnIdle { store.dispatch(MenuAction.Update(groups)) }

        composeTestRule.onNodeWithContentDescription("Footer").assertDoesNotExist()
        assertEquals(originalHeight, composeTestRule.onNodeWithTag("menu").getUnclippedBoundsInRoot().height)
    }

    @Test
    fun `GIVEN a single sticky group THEN show it only once`() {
        setMenu(listOf(stickyGroup("Header")))

        composeTestRule.onAllNodesWithContentDescription("Header").assertCountEquals(1)
        composeTestRule.onNodeWithContentDescription("Header").assertIsDisplayed()
        assertTrue(composeTestRule.onNodeWithTag("menu").getUnclippedBoundsInRoot().height < 300.dp)
    }

    @Test
    fun `GIVEN a sticky grid group THEN do not display visible text and provide it as content description`() {
        setMenu(listOf(stickyGroup("StickyItem")))

        composeTestRule.onNodeWithContentDescription("StickyItem").assertIsDisplayed()
        composeTestRule.onNode(hasText("StickyItem"), useUnmergedTree = true).assertDoesNotExist()
    }

    @Test
    fun `GIVEN top attribution WHEN scrolling THEN keep it above the sticky header`() {
        assertAttributionStaysOutsideScrollingContent(showAtTop = true)
    }

    @Test
    fun `GIVEN bottom attribution WHEN scrolling THEN keep it below the sticky footer`() {
        assertAttributionStaysOutsideScrollingContent(showAtTop = false)
    }

    @Test
    fun `GIVEN a short menu with attribution THEN wrap content and preserve attribution when items update`() {
        val store =
            setMenu(
                groups = listOf(row("Item")),
                attribution = MenuAttribution(Text.String("Powered by Focus"), showAtTop = false, icon = null),
            )

        composeTestRule.onNodeWithText("Powered by Focus").assertIsDisplayed()
        assertTrue(composeTestRule.onNodeWithTag("menu").getUnclippedBoundsInRoot().height < 300.dp)

        composeTestRule.runOnIdle { store.dispatch(MenuAction.Update(listOf(row("Updated item")))) }

        composeTestRule.onNodeWithText("Updated item").assertIsDisplayed()
        composeTestRule.onNodeWithText("Powered by Focus").assertIsDisplayed()
    }

    private fun assertAttributionStaysOutsideScrollingContent(showAtTop: Boolean) {
        setMenu(
            groups = listOf(stickyGroup("Header")) + List(20) { row("Item $it") } + stickyGroup("Footer"),
            attribution = MenuAttribution(Text.String("Powered by Focus"), showAtTop = showAtTop, icon = null),
        )
        val attributionBefore = composeTestRule.onNodeWithText("Powered by Focus").getUnclippedBoundsInRoot()

        composeTestRule.onNode(hasScrollAction()).performScrollToNode(hasText("Item 19"))

        composeTestRule.onNodeWithText("Powered by Focus").assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription("Header").assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription("Footer").assertIsDisplayed()
        composeTestRule.onNodeWithText("Item 19").assertIsDisplayed()
        val attributionAfter = composeTestRule.onNodeWithText("Powered by Focus").getUnclippedBoundsInRoot()
        val headerBounds = composeTestRule.onNodeWithContentDescription("Header").getUnclippedBoundsInRoot()
        val footerBounds = composeTestRule.onNodeWithContentDescription("Footer").getUnclippedBoundsInRoot()
        val listBounds = composeTestRule.onNode(hasScrollAction()).getUnclippedBoundsInRoot()
        val lastItemBounds = composeTestRule.onNodeWithText("Item 19").getUnclippedBoundsInRoot()
        assertEquals(attributionBefore, attributionAfter)
        assertTrue(listBounds.bottom <= footerBounds.top)
        assertTrue(lastItemBounds.bottom <= listBounds.bottom)
        if (showAtTop) {
            assertTrue(attributionAfter.bottom <= headerBounds.top)
        } else {
            assertTrue(footerBounds.bottom <= attributionAfter.top)
        }
    }

    @Test
    fun `GIVEN a banner in a list WHEN showing the menu THEN show its title and subtitle`() {
        setMenu(listOf(MenuItemsGroup.Row(id = "banner", items = listOf(banner))))

        composeTestRule.onNodeWithText(BANNER_TITLE).assertIsDisplayed()
        composeTestRule.onNodeWithText(BANNER_SUBTITLE).assertIsDisplayed()
    }

    @Test
    fun `WHEN clicking a banner THEN dispatch its click event`() {
        val events = mutableListOf<MenuAction>()
        setMenu(listOf(MenuItemsGroup.Row(id = "banner", items = listOf(banner))), middleware = recording(events))

        composeTestRule.onNodeWithText(BANNER_TITLE).performClick()

        assertEquals(listOf<MenuAction>(BannerClicked), events.filterIsInstance<MenuEvent>())
    }

    @Test
    fun `WHEN dismissing a banner THEN dispatch its dismiss event`() {
        val closeButtonContentDescription =
            testContext.getString(MozacMenuR.string.mozac_menu_banner_dismiss_description)
        val events = mutableListOf<MenuAction>()
        setMenu(listOf(MenuItemsGroup.Row(id = "banner", items = listOf(banner))), middleware = recording(events))

        composeTestRule.onNodeWithContentDescription(closeButtonContentDescription).performClick()

        assertEquals(listOf<MenuAction>(BannerDismissed), events.filterIsInstance<MenuEvent>())
    }

    @Test
    fun `GIVEN a banner in a grid WHEN showing the menu THEN don't show it`() {
        setMenu(listOf(MenuItemsGroup.Grid(id = "banner", items = listOf(banner)), row("Item")))

        composeTestRule.onNodeWithText("Item").assertIsDisplayed()
        composeTestRule.onNodeWithText(BANNER_TITLE).assertDoesNotExist()
    }

    @Test
    fun `GIVEN a grid not showing titles in the middle of the menu THEN show its items without titles`() {
        setMenu(listOf(row("Item"), MenuItemsGroup.Grid(id = "grid", items = listOf(item("Grid")), showTitles = false)))

        composeTestRule.onNodeWithContentDescription("Grid").assertIsDisplayed()
        composeTestRule.onNode(hasText("Grid"), useUnmergedTree = true).assertDoesNotExist()
    }

    @Test
    fun `GIVEN a sticky grid item with a content description THEN announce it instead of its hidden title`() {
        val stickyItem = item("Bookmark").copy(contentDescription = Text.String("Bookmarked"))
        setMenu(listOf(MenuItemsGroup.Grid(id = "header", items = listOf(stickyItem), isSticky = true), row("Item")))

        composeTestRule.onNodeWithContentDescription("Bookmarked").assertIsDisplayed()
        composeTestRule.onNodeWithContentDescription("Bookmark").assertDoesNotExist()
        composeTestRule.onNode(hasText("Bookmark"), useUnmergedTree = true).assertDoesNotExist()
    }

    @Test
    fun `GIVEN a grid in the middle of the menu THEN show the titles of its items`() {
        setMenu(listOf(row("Item"), MenuItemsGroup.Grid(id = "grid", items = listOf(item("Grid")))))

        composeTestRule.onNodeWithText("Grid").assertIsDisplayed()
    }

    @Test
    fun `WHEN menu with sticky header is scrollable THEN report dark header background`() {
        var isHeaderDark = false
        val groups = listOf(stickyGroup("Header")) + List(20) { row("Item $it") }
        val store = MenuStore(initialState = MenuState(groups))
        composeTestRule.setContent {
            AcornTheme {
                Box(Modifier.fillMaxSize()) {
                    Menu(
                        store = store,
                        modifier = Modifier.width(320.dp).heightIn(max = 300.dp),
                        onHeaderDarkBackgroundChanged = { isHeaderDark = it },
                    )
                }
            }
        }

        composeTestRule.runOnIdle {
            assertTrue(isHeaderDark)
        }
    }

    @Test
    fun `GIVEN a visible item with a shown event THEN dispatch it only after the item was visible long enough`() {
        composeTestRule.mainClock.autoAdvance = false
        val events = mutableListOf<MenuAction>()
        setMenu(listOf(row("Item"), shownRow()), middleware = recording(events))

        composeTestRule.mainClock.advanceTimeBy(SHORTER_THAN_SHOWN_DURATION_MS)
        assertEquals(emptyList<MenuAction>(), events.filterIsInstance<MenuEvent>())

        composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)
        assertEquals(listOf<MenuAction>(ItemShown), events.filterIsInstance<MenuEvent>())
    }

    @Test
    fun `GIVEN an item with a shown event WHEN scrolling it out of the menu and back THEN dispatch it only once`() {
        val events = mutableListOf<MenuAction>()
        setMenu(listOf(shownRow()) + List(20) { row("Item $it") }, middleware = recording(events))
        composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)

        composeTestRule.onNode(hasScrollAction()).performScrollToNode(hasText("Item 19"))
        composeTestRule.onNodeWithText(SHOWN_TITLE).assertDoesNotExist()
        composeTestRule.onNode(hasScrollAction()).performScrollToNode(hasText(SHOWN_TITLE))
        composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)

        assertEquals(listOf<MenuAction>(ItemShown), events.filterIsInstance<MenuEvent>())
    }

    @Test
    fun `GIVEN an item with a shown event laid out below the visible part of the menu THEN dispatch it only once scrolled into view`() {
        val events = mutableListOf<MenuAction>()
        val group = MenuItemsGroup.Row(id = "group", items = List(4) { item("Item $it") } + shownItem())
        setMenu(listOf(group), middleware = recording(events), maxHeight = 150.dp)
        composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)

        val listBounds = composeTestRule.onNode(hasScrollAction()).getUnclippedBoundsInRoot()
        val itemBounds = composeTestRule.onNodeWithText(SHOWN_TITLE).getUnclippedBoundsInRoot()
        val windowBounds = composeTestRule.onRoot().getUnclippedBoundsInRoot()
        assertTrue(itemBounds.top >= listBounds.bottom)
        assertTrue(itemBounds.bottom <= windowBounds.bottom)
        assertEquals(emptyList<MenuAction>(), events.filterIsInstance<MenuEvent>())

        composeTestRule.onNode(hasScrollAction()).performScrollToNode(hasText(SHOWN_TITLE))
        composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)

        assertEquals(listOf<MenuAction>(ItemShown), events.filterIsInstance<MenuEvent>())
    }

    @Test
    fun `GIVEN a sub-item with a shown event WHEN its parent is expanded THEN dispatch it only after the sub-item was visible long enough`() {
        val events = mutableListOf<MenuAction>()
        val more =
            ExpandableMenuItem(
                title = MenuItemTitle(Text.String("More")),
                onClickEvent = object : MenuEvent {},
                subMenuItems = listOf(shownItem()),
                hideOnExpand = true,
            )
        setMenu(listOf(MenuItemsGroup.Row(id = "more", items = listOf(more))), middleware = recording(events))
        composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)
        assertEquals(emptyList<MenuAction>(), events.filterIsInstance<ItemShown>())

        composeTestRule.mainClock.autoAdvance = false
        composeTestRule.onNodeWithText("More").performClick()
        composeTestRule.mainClock.advanceTimeBy(EXPAND_ANIMATION_MS + SHORTER_THAN_SHOWN_DURATION_MS)
        assertEquals(emptyList<MenuAction>(), events.filterIsInstance<ItemShown>())

        composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)
        assertEquals(listOf<MenuAction>(ItemShown), events.filterIsInstance<ItemShown>())
    }

    @Test
    fun `GIVEN an item with a shown event WHEN it is scrolled away before the minimum duration THEN don't dispatch it`() {
        val events = mutableListOf<MenuAction>()
        setMenu(listOf(shownRow()) + List(20) { row("Item $it") }, middleware = recording(events))
        composeTestRule.mainClock.advanceTimeBy(SHORTER_THAN_SHOWN_DURATION_MS)

        composeTestRule.onNode(hasScrollAction()).performScrollToNode(hasText("Item 19"))
        composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)

        assertEquals(emptyList<MenuAction>(), events.filterIsInstance<ItemShown>())
    }

    @Test
    fun `GIVEN a shown item WHEN an item is inserted before it while the menu is open THEN don't dispatch it again`() {
        val events = mutableListOf<MenuAction>()
        val store =
            setMenu(
                listOf(MenuItemsGroup.Row(id = "group", items = listOf(shownItem()))),
                middleware = recording(events),
            )
        composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)

        composeTestRule.runOnIdle {
            store.dispatch(
                MenuAction.Update(
                    listOf(MenuItemsGroup.Row(id = "group", items = listOf(item("Inserted"), shownItem())))
                )
            )
        }
        composeTestRule.onNodeWithText("Inserted").assertIsDisplayed()
        composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)

        assertEquals(listOf<MenuAction>(ItemShown), events.filterIsInstance<ItemShown>())
    }

    @Test
    fun `GIVEN an item inside the menu but below the bottom of the window THEN don't dispatch its shown event`() {
        val events = mutableListOf<MenuAction>()
        val group = MenuItemsGroup.Row(id = "group", items = List(2) { item("Item $it") } + shownItem())
        setMenu(listOf(group), middleware = recording(events), menuOffset = 400.dp)
        composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)

        val windowBounds = composeTestRule.onRoot().getUnclippedBoundsInRoot()
        val listBounds = composeTestRule.onNode(hasScrollAction()).getUnclippedBoundsInRoot()
        val itemBounds = composeTestRule.onNodeWithText(SHOWN_TITLE).getUnclippedBoundsInRoot()
        assertTrue(itemBounds.top >= windowBounds.bottom)
        assertTrue(itemBounds.bottom <= listBounds.bottom)

        assertEquals(emptyList<MenuAction>(), events.filterIsInstance<ItemShown>())
    }

    @Test
    fun `GIVEN a shown sub-item WHEN collapsing and expanding its parent again THEN don't dispatch its shown event again`() {
        val events = mutableListOf<MenuAction>()
        val section =
            ExpandableMenuItem(
                title = MenuItemTitle(Text.String("Section")),
                onClickEvent = object : MenuEvent {},
                subMenuItems = listOf(shownItem()),
                hideOnExpand = false,
            )
        setMenu(listOf(MenuItemsGroup.Row(id = "section", items = listOf(section))), middleware = recording(events))

        repeat(2) {
            composeTestRule.onNodeWithText("Section").performClick()
            composeTestRule.onNodeWithText(SHOWN_TITLE).assertIsDisplayed()
            composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)
            composeTestRule.onNodeWithText("Section").performClick()
            composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)
            composeTestRule.onNodeWithText(SHOWN_TITLE).assertDoesNotExist()
        }

        assertEquals(listOf<MenuAction>(ItemShown), events.filterIsInstance<ItemShown>())
    }

    @Test
    fun `GIVEN an item whose shown event was dispatched WHEN the menu is recreated THEN don't dispatch it again`() {
        val events = mutableListOf<MenuAction>()
        val store = MenuStore(initialState = MenuState(listOf(shownRow())), middleware = recording(events))
        val restorationTester = StateRestorationTester(composeTestRule)
        restorationTester.setContent {
            AcornTheme {
                Box(Modifier.fillMaxSize()) {
                    Menu(store, Modifier.width(320.dp).heightIn(max = 300.dp))
                }
            }
        }
        composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)

        restorationTester.emulateSavedInstanceStateRestore()
        composeTestRule.mainClock.advanceTimeBy(SHOWN_DURATION_MS)

        assertEquals(listOf<MenuAction>(ItemShown), events.filterIsInstance<MenuEvent>())
    }

    private fun recording(events: MutableList<MenuAction>): List<Middleware<MenuState, MenuAction>> =
        listOf { _, next, action ->
            events += action
            next(action)
        }

    private fun setMenu(
        groups: List<MenuItemsGroup>,
        attribution: MenuAttribution? = null,
        middleware: List<Middleware<MenuState, MenuAction>> = emptyList(),
        maxHeight: Dp = 300.dp,
        menuOffset: Dp = 0.dp,
    ): MenuStore {
        val store = MenuStore(initialState = MenuState(groups, attribution), middleware = middleware)
        composeTestRule.setContent {
            AcornTheme {
                Box(Modifier.fillMaxSize()) {
                    Menu(
                        store,
                        Modifier.offset(y = menuOffset).width(320.dp).heightIn(max = maxHeight).testTag("menu"),
                    )
                }
            }
        }
        return store
    }

    private fun row(title: String) = MenuItemsGroup.Row(id = title, items = listOf(item(title)))

    private fun stickyGroup(title: String) =
        MenuItemsGroup.Grid(id = title, items = listOf(item(title)), isSticky = true)

    private fun shownRow() = MenuItemsGroup.Row(id = SHOWN_TITLE, items = listOf(shownItem()))

    private fun shownItem() = item(SHOWN_TITLE).copy(onShownEvent = ItemShown)

    private data object ItemShown : MenuImpressionEvent {
        override val impressionId = "shown_item"
    }

    private data object BannerClicked : MenuEvent

    private data object BannerDismissed : MenuEvent

    private val banner =
        BannerMenuItem(
            title = MenuItemTitle(Text.String(BANNER_TITLE)),
            summary = MenuItemSummary(Text.String(BANNER_SUBTITLE)),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_globe_24),
            onClickEvent = BannerClicked,
            onDismissEvent = BannerDismissed,
        )

    private fun item(title: String) =
        StandardMenuItem(
            title = MenuItemTitle(Text.String(title)),
            icon = MenuItemIconRes(iconsR.drawable.mozac_ic_settings_24),
            onClickEvent = object : MenuEvent {},
        )

    private companion object {
        const val BANNER_TITLE = "Banner title"
        const val BANNER_SUBTITLE = "Banner subtitle"
        const val SHOWN_TITLE = "Shown item"
        const val SHOWN_DURATION_MS = 600L
        const val SHORTER_THAN_SHOWN_DURATION_MS = 300L
        const val EXPAND_ANIMATION_MS = 300L
    }
}
