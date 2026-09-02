/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.components.tabgroups

import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.navigation.TabManagerNavDestination

/** List of valid destinations for the Tab Group feature flow outside of the TabsTray. */
sealed interface TabGroupFlowDestination {
    /** Supported actions (public API) */
    enum class SupportedActions {
        EDIT,
        ADD,
        VIEW,
    }

    /** [TabManagerNavDestination] representing the Root. Displays no content, is a base for the bottom sheet. */
    data object Root : TabGroupFlowDestination

    /** [TabManagerNavDestination] representing the [org.mozilla.fenix.tabgroups.EditTabGroup]. */
    data object EditTabGroup : TabGroupFlowDestination

    /** [TabManagerNavDestination] representing the [org.mozilla.fenix.tabgroups.AddToTabGroup]. */
    data class AddToTabGroup(val tabId: String) : TabGroupFlowDestination

    /**
     * [TabManagerNavDestination] representing the [org.mozilla.fenix.tabgroups.ExpandedTabGroup].
     *
     * @property group The displayed [TabsTrayItem.TabGroup].
     */
    data class ExpandedTabGroup(val group: TabsTrayItem.TabGroup) : TabGroupFlowDestination
}
