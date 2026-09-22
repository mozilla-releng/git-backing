/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.tabgroups.storage

import mozilla.components.feature.tabgroups.storage.data.TabGroup
import mozilla.components.feature.tabgroups.storage.repository.TabGroupRepository
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.utils.DateTimeProvider
import org.mozilla.fenix.tabstray.data.TabGroupTheme

/**
 * Many tab group operations contain sequences that must happen in a particular order, but cannot be enforced at an ACID
 * level because TabGroup data is stored in SQL and BrowserState tab data is stored in JSON. This class implements
 * re-usable operations that encode these operations.
 *
 * To better separate concerns, this class should not depend on BrowserState.
 */
class TabGroupStorageOperations(
    val tabGroupRepository: TabGroupRepository,
    val moveTabsUseCase: TabsUseCases.MoveTabsUseCase,
    val dateTimeProvider: DateTimeProvider,
) {
    /**
     * Add a single tab into a group. The tas should be added to the end of the group's tab list.
     *
     * @param groupId the target group
     * @param tabId the tab being added
     * @param sequenceAfterTabId the tab id to sequence the target tabs after
     */
    suspend fun addTabItemToTabGroup(
        groupId: String,
        tabId: String,
        sequenceAfterTabId: String?,
    ) {
        moveTabsUseCase.invoke(
            sourceTabId = tabId,
            targetTabId = sequenceAfterTabId,
            placeAfter = true,
        )
        tabGroupRepository.addTabGroupAssignment(
            tabId = tabId,
            tabGroupId = groupId,
        )
    }

    /**
     * Add a list of tabs into a group. The tabs should be added to the end of the group's tab list.
     *
     * @param groupId the target group
     * @param tabIds the list of tabs being added
     * @param sequenceAfterTabId the tab id to sequence the target tabs after
     */
    suspend fun addTabItemsToTabGroup(
        groupId: String,
        tabIds: List<String>,
        sequenceAfterTabId: String?,
    ) {
        // Sequence the selected tabs after the group's other tabs, if it has any.
        sequenceAfterTabId?.let {
            sequenceGroupedTabsTogether(
                tabIds = tabIds,
                targetTabId = sequenceAfterTabId,
            )
        }

        tabGroupRepository.addTabsToTabGroup(
            tabGroupId = groupId,
            tabIds = tabIds,
        )
    }

    /**
     * Add a group on top of a tab (by dragging and dropping). Note the distinction here from adding a tab to a group -
     * The user should see the group show up where the tab was, so the group's tabs are prepended to the tab.
     *
     * @param groupId the group being moved onto a tab
     * @param tabId the target tab
     * @param groupedTabs the group's existing tabs (can be empty)
     */
    suspend fun addGroupToTab(
        groupId: String,
        tabId: String,
        groupedTabs: List<String>,
    ) {
        if (groupedTabs.isNotEmpty()) {
            moveTabsUseCase.invoke(
                tabIds = groupedTabs,
                targetTabId = tabId,
                placeAfter = false,
            )
        }
        tabGroupRepository.addTabGroupAssignment(
            tabId = tabId,
            tabGroupId = groupId,
        )
    }

    /**
     * Create a TabGroup with a list of tab ids.
     *
     * @param title The title of the group
     * @param theme The group's color theme
     * @param tabIds The list of tabs to be in the group
     */
    suspend fun createGroup(
        title: String,
        theme: TabGroupTheme,
        tabIds: List<String>,
    ): String {
        val tabGroup =
            TabGroup(
                title = title,
                theme = theme.toStorageValue(),
                lastModified = dateTimeProvider.currentTimeMillis(),
                tabIds = tabIds,
            )
        tabGroupRepository.createTabGroupWithTabs(tabGroup = tabGroup)
        return tabGroup.id
    }

    /**
     * Update a TabGroup.
     *
     * @param id The group's identifier
     * @param title The updated title
     * @param theme The updated color theme
     */
    suspend fun updateGroup(
        id: String,
        title: String,
        theme: TabGroupTheme,
        tabIds: List<String>,
    ) {
        tabGroupRepository.updateTabGroup(
            TabGroup(
                id = id,
                title = title,
                theme = theme.toStorageValue(),
                tabIds = tabIds,
                lastModified = dateTimeProvider.currentTimeMillis(),
            )
        )
    }

    // Because the sort order is defined by the underlying JSON file, we need to arrange all the group's tabs
    // next to each other in BrowserState so they are correctly sorted/grouped together. This is
    // necessary to ensure the downstream tab group is indexed at the correct spot when displayed
    // in the grid/list.
    private fun sequenceGroupedTabsTogether(
        tabIds: List<String>,
        targetTabId: String,
    ) {
        moveTabsUseCase.invoke(
            tabIds = tabIds,
            targetTabId = targetTabId,
            placeAfter = true,
        )
    }

    internal fun TabGroupTheme.toStorageValue(): String = name
}
