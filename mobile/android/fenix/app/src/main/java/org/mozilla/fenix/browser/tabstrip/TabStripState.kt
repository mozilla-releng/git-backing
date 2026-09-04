/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.tabstrip

import android.graphics.Bitmap
import mozilla.components.browser.state.selector.getNormalOrPrivateTabs
import mozilla.components.browser.state.selector.selectedTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TabSessionState
import org.mozilla.fenix.tabgroups.storage.data.TabGroupData
import org.mozilla.fenix.tabstray.data.TabGroupTheme

private const val MAX_TABS_WITH_CLOSE_BUTTON_VISIBLE = 7

/**
 * The ui state of the tabs strip.
 *
 * @property tabs Flat list of tabs (for counters and menus).
 * @property entries Tabs and expanded groups to render in strip order.
 * @property isPrivateMode Whether or not the browser is in private mode.
 * @property tabCounterMenuItems The list of [TabCounterMenuItem]s to be displayed in the tab
 * counter menu.
 */
data class TabStripState(
    val tabs: List<TabStripItem>,
    val entries: List<TabStripEntry> = tabs.map { TabStripEntry.Tab(it) },
    val isPrivateMode: Boolean,
    val tabCounterMenuItems: List<TabCounterMenuItem>,
) {

    val menuItems
        get() = tabCounterMenuItems.map { it.toMenuItem() }

    companion object {
        val initial = TabStripState(
            tabs = emptyList(),
            entries = emptyList(),
            isPrivateMode = false,
            tabCounterMenuItems = emptyList(),
        )
    }
}

/**
 * A renderable item in the tablet tab strip: a lone tab or an expanded tab group.
 */
sealed interface TabStripEntry {
    /** Stable LazyRow key. */
    val key: String

    /**
     * An ungrouped tab.
     */
    data class Tab(
        val item: TabStripItem,
    ) : TabStripEntry {
        override val key: String = item.id
    }

    /**
     * An expanded tab group: name chip + member tabs + colored underline (state 1).
     */
    data class Group(
        val id: String,
        val title: String,
        val theme: TabGroupTheme,
        val tabs: List<TabStripItem>,
    ) : TabStripEntry {
        override val key: String = "group:$id"
    }
}

/**
 * The ui state of a tab.
 *
 * @property id The id of the tab.
 * @property title The title of the tab.
 * @property url The url of the tab.
 * @property icon The icon of the tab.
 * @property isPrivate Whether or not the tab is private.
 * @property isSelected Whether or not the tab is selected.
 * @property isCloseButtonVisible Whether or not the close button is visible.
 */
data class TabStripItem(
    val id: String,
    val title: String,
    val url: String,
    val icon: Bitmap? = null,
    val isPrivate: Boolean,
    val isSelected: Boolean,
    val isCloseButtonVisible: Boolean = true,
)

/**
 * Converts [BrowserState] to [TabStripState] that contains the information needed to render the
 * tabs strip. [TabStripState.isPrivateMode] is determined by the selected tab's privacy state when
 * [isSelectDisabled] is false. Otherwise, the private mode is determined by [isPossiblyPrivateMode].
 *
 * @param isSelectDisabled When true, the tabs will show as unselected.
 * @param isPossiblyPrivateMode Whether or not the browser is in private mode.
 * @param addTab Invoked when conditions are met for adding a new normal browsing mode tab.
 * @param closeTab Invoked when close tab is clicked.
 * @param tabGroupData Optional open-group data used to cluster tabs into expanded group entries.
 */
internal fun BrowserState.toTabStripState(
    isSelectDisabled: Boolean,
    isPossiblyPrivateMode: Boolean,
    addTab: () -> Unit,
    closeTab: (isPrivate: Boolean, numberOfTabs: Int) -> Unit,
    tabGroupData: TabGroupData? = null,
): TabStripState {
    val isPrivateMode = if (isSelectDisabled) {
        isPossiblyPrivateMode
    } else {
        selectedTab?.content?.private == true
    }

    val sessionTabs = getNormalOrPrivateTabs(private = isPrivateMode)
    val tabs = sessionTabs.map {
        it.toTabStripItem(
            isSelectDisabled = isSelectDisabled,
            selectedTabId = selectedTabId,
            showCloseButtonOnUnselectedTabs = sessionTabs.size <= MAX_TABS_WITH_CLOSE_BUTTON_VISIBLE,
        )
    }

    return TabStripState(
        tabs = tabs,
        entries = tabs.toTabStripEntries(tabGroupData),
        isPrivateMode = isPrivateMode,
        tabCounterMenuItems = mapToMenuItems(
            isSelectEnabled = !isSelectDisabled,
            isPrivateMode = isPrivateMode,
            addTab = addTab,
            closeTab = closeTab,
            numberOfTabs = tabs.size,
        ),
    )
}

/**
 * Clusters [TabStripItem]s into strip entries. Open groups are emitted once at the position of
 * their first member; later members are folded into that group (tray-style ordering).
 */
internal fun List<TabStripItem>.toTabStripEntries(
    tabGroupData: TabGroupData?,
): List<TabStripEntry> {
    if (tabGroupData == null || isEmpty()) {
        return map { TabStripEntry.Tab(it) }
    }

    val openGroups = tabGroupData.tabGroups
        .filter { !it.closed }
        .associateBy { it.id }
    if (openGroups.isEmpty()) {
        return map { TabStripEntry.Tab(it) }
    }

    val assignments = tabGroupData.tabGroupAssignments
    val emittedGroupIds = mutableSetOf<String>()
    val entries = mutableListOf<TabStripEntry>()

    for (tab in this) {
        val groupId = assignments[tab.id]
        val storedGroup = groupId?.let { openGroups[it] }
        if (groupId == null || storedGroup == null) {
            entries.add(TabStripEntry.Tab(tab))
            continue
        }
        if (groupId in emittedGroupIds) {
            continue
        }
        val members = filter { assignments[it.id] == groupId }
        if (members.isEmpty()) {
            continue
        }
        emittedGroupIds.add(groupId)
        entries.add(
            TabStripEntry.Group(
                id = storedGroup.id,
                title = storedGroup.title,
                theme = storedGroup.theme.toTabGroupTheme(),
                tabs = members,
            ),
        )
    }

    return entries
}

private fun mapToMenuItems(
    isSelectEnabled: Boolean,
    isPrivateMode: Boolean,
    addTab: () -> Unit,
    closeTab: (isPrivate: Boolean, numberOfTabs: Int) -> Unit,
    numberOfTabs: Int,
): List<TabCounterMenuItem> = buildList {
    if (isSelectEnabled || isPrivateMode) {
        add(TabCounterMenuItem.IconItem.NewTab(onClick = addTab))
    }

    if (isSelectEnabled) {
        add(TabCounterMenuItem.Divider)
        add(
            TabCounterMenuItem.IconItem.CloseTab(
                onClick = { closeTab(isPrivateMode, numberOfTabs) },
            ),
        )
    }
}

private fun TabSessionState.toTabStripItem(
    isSelectDisabled: Boolean,
    selectedTabId: String?,
    showCloseButtonOnUnselectedTabs: Boolean,
): TabStripItem {
    val isSelected = !isSelectDisabled && id == selectedTabId
    return TabStripItem(
        id = id,
        title = content.title.ifBlank { content.url },
        url = content.url,
        icon = content.icon,
        isPrivate = content.private,
        isSelected = isSelected,
        isCloseButtonVisible = showCloseButtonOnUnselectedTabs || isSelected,
    )
}

private fun String.toTabGroupTheme(): TabGroupTheme = try {
    TabGroupTheme.valueOf(this)
} catch (_: IllegalArgumentException) {
    TabGroupTheme.default
}
