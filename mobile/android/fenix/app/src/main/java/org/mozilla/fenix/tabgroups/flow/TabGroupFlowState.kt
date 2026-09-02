/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.tabgroups.flow

import mozilla.components.lib.state.State
import org.mozilla.fenix.components.tabgroups.TabGroupFlowDestination
import org.mozilla.fenix.tabgroups.EditTabGroup
import org.mozilla.fenix.tabgroups.TabGroupTelemetry
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState

/**
 * @property backStack The flow's backstack
 * @property groups The list of tab groups.
 * @property formState The state of the tab group edit form.
 * @property config The Config for the flow
 */
data class TabGroupFlowState(
    val backStack: List<TabGroupFlowDestination> = listOf(TabGroupFlowDestination.Root),
    override val groups: List<TabsTrayItem.TabGroup> = emptyList(),
    override val formState: TabGroupFormState? = null,
    val config: Config = Config(),
) : State, TabGroupTelemetry.TabGroupTelemetryContext {
    /** Drops the last entry of [TabsTrayState.backStack]. If [backStack] only has one entry, no changes occur. */
    internal fun popBackStack(): List<TabGroupFlowDestination> =
        if (backStack.size > 1) {
            backStack.dropLast(1)
        } else {
            backStack
        }

    /**
     * Configuration for the flow of Tab Groups outside the TabsTray.
     *
     * @property displayTabsInGrid Whether normal and private tabs are displayed in a grid (vs list).
     * @property homepageAsNewTabEnabled Whether the homepage as a new tab feature is enabled, which gates the Tab
     *   Groups create FAB.
     * @property isInDebugMode Whether the app is in a debug state or has secret menu enabled.
     */
    data class Config(
        val displayTabsInGrid: Boolean = false,
        val homepageAsNewTabEnabled: Boolean = false,
        val isInDebugMode: Boolean = false,
    )

    /**
     * Returns an initial [TabGroupFormState] derived from [TabGroupFlowState].
     *
     * Note: Because we need a localized string for the initial name, this is constructed at render time in
     * [EditTabGroup].
     */
    fun TabGroupFlowState.initializeTabGroupForm(isStarterTabGroup: Boolean = false) =
        TabGroupFormState(
            tabGroupId = null,
            name = "",
            nextTabGroupNumber = groups.size + 1,
            theme = groups.maxByOrNull { it.lastModified }?.theme?.next() ?: TabGroupTheme.default,
            edited = false,
            isStarterTabGroup = isStarterTabGroup,
        )
}
