/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Metrics
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.components.metrics.MetricsUtils.TabGroupAccessPoint
import org.mozilla.fenix.tabstray.data.TabGroupTheme
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState

/**
 * Class that handles Telemetry for the TabGroups feature.
 *
 * @param tabGroupAccessPoint - [TabGroupAccessPoint] where TabGroups feature was accessed
 */
class TabGroupTelemetry(private val tabGroupAccessPoint: TabGroupAccessPoint) {
    /**
     * Context interface wrapper that allows different state objects to serve as the context for invoking TabGroup
     * telemetry actions.
     */
    interface TabGroupTelemetryContext {
        val groups: List<TabsTrayItem.TabGroup>
        val formState: TabGroupFormState?
    }

    /**
     * Records a TabGroup save action.
     *
     * @param context: [TabGroupTelemetryContext] context object
     */
    fun recordSaveClicked(context: TabGroupTelemetryContext) {
        val formState = context.formState ?: return
        if (formState.inEditState) {
            val originalGroup = context.groups.find { it.id == formState.tabGroupId }
            if (originalGroup != null && originalGroup.title != formState.name) {
                TabsTray.tabGroupNameChanged.record(NoExtras())
            }
        } else {
            TabsTray.tabGroupCreated.record(NoExtras())
            TabsTray.tabGroupNamed.record(NoExtras())
        }
    }

    /**
     * Records a TabGroup theme change action.
     *
     * @param context: [TabGroupTelemetryContext] context object
     * @param theme: [TabGroupTheme] being recorded
     */
    fun recordThemeChanged(context: TabGroupTelemetryContext, theme: TabGroupTheme) {
        val formState = context.formState ?: return
        if (formState.inEditState) {
            TabsTray.tabGroupColorChanged.record(TabsTray.TabGroupColorChangedExtra(theme.name))
        } else {
            TabsTray.tabGroupColorAssigned.record(TabsTray.TabGroupColorAssignedExtra(theme.name))
        }
    }

    /**
     * Records a tab added to a group.
     *
     * @param tabCount: Number of tabs being added
     */
    fun recordTabAddedToGroup(tabCount: Int) {
        TabsTray.tabAddedToGroup.record(TabsTray.TabAddedToGroupExtra(tabCount = tabCount))
    }

    /** Records a TabGroup creation from a menu. */
    fun recordCreatedFromMenu() {
        val label =
            when (tabGroupAccessPoint) {
                TabGroupAccessPoint.TABS_TRAY -> "menu"
                TabGroupAccessPoint.BROWSER_MENU -> "browser_menu"
            }
        Metrics.tabGroupCreationMode[label].add()
    }

    /** Records a TabGroup creation from a Floating Action Button. */
    fun recordCreatedFromFab() {
        Metrics.tabGroupCreationMode["fab"].add()
    }

    /** Records a TabGroup creation from a Drag and Drop action. */
    fun recordCreatedFromDragAndDrop() {
        Metrics.tabGroupCreationMode["drag_and_drop"].add()
    }
}
