/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
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
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.menu.MenuFragmentDirections
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.tabgroups.flow.TabGroupFlowEntryPoint

/**
 * [MenuItemProvider] for TabGroup menu items. Allows adding a tab to a new or existing tab group, or moving a tab
 * already in a tab group to a new or existing tab group.
 *
 * The item is highlighted while [SupportedMenuNotifications.TabGroups] is set in the [AppStore], the same notification
 * that highlights the toolbar menu button, so that both stay in sync.
 *
 * @param tabDataCoordinator [TabDataCoordinator] determines whether the selected tab is private or already in a group.
 * @param appStore [AppStore] to observe whether the item should be highlighted.
 * @param discoveryConfiguration [TabGroupsFeatureDiscoveryConfiguration] recording when the item is shown and used.
 * @param target [MenuTarget] target this menu item acts on.
 * @param isVisible Whether the item can be shown in the menu.
 * @param scope [CoroutineScope] used to keep the item up to date for as long as it can be shown.
 */
class TabGroupMenuItemProvider(
    tabDataCoordinator: TabDataCoordinator,
    appStore: AppStore,
    private val discoveryConfiguration: TabGroupsFeatureDiscoveryConfiguration,
    target: MenuTarget,
    isVisible: Boolean,
    scope: CoroutineScope,
) : MenuItemProvider {
    companion object {
        /** The item for adding a tab to a group, without events or highlight, as shown when customizing the menu. */
        val ADD_ITEM: StandardMenuItem =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_add_to_tab_group)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_tab_group_24, isHighlighted = false),
            )
    }

    override val itemsFlow: StateFlow<List<MenuItem>> =
        when {
            !isVisible -> MutableStateFlow(emptyList())
            target == MenuTarget.CustomizeMenu -> MutableStateFlow(listOf(ADD_ITEM))
            target != MenuTarget.BrowserTab -> MutableStateFlow(emptyList())
            else -> {
                val isHighlighted =
                    appStore.stateFlow
                        .map {
                            SupportedMenuNotifications.TabGroups in it.supportedMenuNotifications
                        }
                        .distinctUntilChanged()
                combine(tabDataCoordinator.tabsSnapshot, isHighlighted) { snapshot, highlighted ->
                        listOfNotNull(snapshot?.toMenuItem(isHighlighted = highlighted))
                    }
                    .stateIn(
                        scope = scope,
                        started = SharingStarted.Eagerly,
                        initialValue =
                            listOfNotNull(
                                (tabDataCoordinator.tabsSnapshot.value?.toMenuItem(
                                    isHighlighted =
                                        SupportedMenuNotifications.TabGroups in
                                            appStore.state.supportedMenuNotifications
                                ))
                            ),
                    )
            }
        }

    private fun TabsSnapshot.toMenuItem(isHighlighted: Boolean): MenuItem? {
        val tab = selectedTab ?: return null
        if (tab.private) return null
        return if (tab.tabGroupId != null) {
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_move_to_tab_group)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_tab_group_24, isHighlighted = isHighlighted),
                onShownEvent = MenuAction.OnTabGroupMenuItemExposed,
                showNewIndicator = isHighlighted,
                onClickEvent = MenuAction.Navigate.OpenTabGroupFlow(TabGroupFlowEntryPoint.MoveTabToGroup(tab.id)),
            )
        } else {
            ADD_ITEM.copy(
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_tab_group_24, isHighlighted = isHighlighted),
                onShownEvent = MenuAction.OnTabGroupMenuItemExposed,
                showNewIndicator = isHighlighted,
                onClickEvent = MenuAction.Navigate.OpenTabGroupFlow(TabGroupFlowEntryPoint.AddTabToGroup(tab.id)),
            )
        }
    }

    override fun handles(event: MenuEvent) =
        event is MenuAction.Navigate.OpenTabGroupFlow || event == MenuAction.OnTabGroupMenuItemExposed

    override fun onEvent(
        event: MenuEvent,
        menu: MenuHost,
    ) {
        when (event) {
            MenuAction.OnTabGroupMenuItemExposed -> discoveryConfiguration.recordMenuItemExposure()
            is MenuAction.Navigate.OpenTabGroupFlow -> {
                discoveryConfiguration.recordMenuItemUsed()
                val entryPoint = event.entryPoint
                menu.navigate(MenuFragmentDirections.actionMenuFragmentToTabGroupFlowFragment(entryPoint = entryPoint))
            }
            else -> Unit
        }
    }
}
