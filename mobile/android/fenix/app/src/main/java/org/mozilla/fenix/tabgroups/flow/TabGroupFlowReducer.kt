/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.tabgroups.flow

import org.mozilla.fenix.components.tabgroups.TabGroupFlowDestination
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction

/** Reducer for the TabGroup flow. */
object TabGroupFlowReducer {
    /** Updates the [TabGroupFlowState] object based on the received [TabGroupAction] */
    fun reduce(
        state: TabGroupFlowState,
        action: TabGroupAction,
    ): TabGroupFlowState {
        return when (action) {
            is TabGroupAction.NavigateBackInvoked -> {
                state.copy(backStack = state.popBackStack())
            }
            is TabGroupAction.TabAddedToNewTabGroup -> {
                state.navigateToCreateTabGroup(isStarterTabGroup = false)
            }
            is TabGroupAction.TabAddedToExistingTabGroup -> {
                state.copy(backStack = state.popBackStack())
            }

            // no op actions
            is TabGroupAction.CloseTabAndDeleteGroupConfirmed,
            is TabGroupAction.CloseTabGroupClicked,
            is TabGroupAction.DeleteClicked,
            is TabGroupAction.DeleteConfirmed,
            is TabGroupAction.DragAndDropInitiated,
            TabGroupAction.DragAndDropProcessed,
            is TabGroupAction.DragAndDropTwoTabs,
            is TabGroupAction.EditTabGroupClicked,
            is TabGroupAction.NameChanged,
            TabGroupAction.NewGroupAnimationFinished,
            is TabGroupAction.NewGroupCreated,
            TabGroupAction.NewTabGroupFabClicked,
            TabGroupAction.NewTabGroupMenuClicked,
            is TabGroupAction.OpenCreatedTabGroup,
            is TabGroupAction.OpenTabGroupClicked,
            TabGroupAction.SaveClicked,
            is TabGroupAction.SelectedTabsAddedToGroup,
            is TabGroupAction.TabClosed,
            is TabGroupAction.TabGroupClicked,
            is TabGroupAction.ThemeChanged,
            is TabGroupAction.ReorderTabGroupItem,
            is TabGroupAction.AddToTabGroup,
            is TabGroupAction.AddToNewTabGroup,
            // Onboarding actions - not shown outside TabsTray
            TabGroupAction.OnboardingDismissed -> state
            TabGroupAction.OnboardingShown -> state
        }
    }

    private fun TabGroupFlowState.navigateToCreateTabGroup(isStarterTabGroup: Boolean = false) =
        copy(
            formState = initializeTabGroupForm(isStarterTabGroup = isStarterTabGroup),
            backStack = backStack + TabGroupFlowDestination.EditTabGroup,
        )
}
