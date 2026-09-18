/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.feature.tabdata.coordinator.TabDataCoordinator
import mozilla.components.feature.tabdata.coordinator.data.TabsSnapshot
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuFragmentDirections
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.tabgroups.flow.TabGroupFlowEntryPoint

/**
 * [MenuItemProvider] for TabGroup menu items. Allows adding a tab to a new or existing tab group, or moving a tab
 * already in a tab group to a new or existing tab group.
 */
class TabGroupMenuItemProvider(
    tabDataCoordinator: TabDataCoordinator,
    target: MenuTarget,
    isEnabled: Boolean,
    scope: CoroutineScope,
) : MenuItemProvider {
    companion object {
        val ADD_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_add_to_tab_group)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_tab_group_24, isHighlighted = true),
                showNewIndicator = true,
            )

        val MOVE_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_move_to_tab_group)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_tab_group_24, isHighlighted = true),
                showNewIndicator = true,
            )
    }

    override val itemsFlow: StateFlow<List<MenuItem>> =
        when {
            !isEnabled -> MutableStateFlow(emptyList())
            target == MenuTarget.CustomizeMenu -> MutableStateFlow(listOf(ADD_ITEM))
            target != MenuTarget.BrowserTab -> MutableStateFlow(emptyList())
            else -> {
                tabDataCoordinator.tabsSnapshot
                    .map {
                        listOfNotNull(it?.toMenuItem())
                    }
                    .stateIn(
                        scope = scope,
                        started = SharingStarted.Eagerly,
                        initialValue = listOfNotNull((tabDataCoordinator.tabsSnapshot.value?.toMenuItem())),
                    )
            }
        }

    private fun TabsSnapshot.toMenuItem(): MenuItem? {
        val tab = selectedTab ?: return null
        if (tab.private) return null
        return if (tab.tabGroupId != null) {
            MOVE_ITEM.copy(
                onClickEvent = MenuAction.Navigate.OpenTabGroupFlow(TabGroupFlowEntryPoint.MoveTabToGroup(tab.id))
            )
        } else {
            ADD_ITEM.copy(
                onClickEvent = MenuAction.Navigate.OpenTabGroupFlow(TabGroupFlowEntryPoint.AddTabToGroup(tab.id))
            )
        }
    }

    override fun handles(event: MenuEvent) = event is MenuAction.Navigate.OpenTabGroupFlow

    override fun onEvent(
        event: MenuEvent,
        menu: MenuHost,
    ) {
        val entryPoint = (event as MenuAction.Navigate.OpenTabGroupFlow).entryPoint
        menu.navigate(MenuFragmentDirections.actionMenuFragmentToTabGroupFlowFragment(entryPoint = entryPoint))
    }
}
