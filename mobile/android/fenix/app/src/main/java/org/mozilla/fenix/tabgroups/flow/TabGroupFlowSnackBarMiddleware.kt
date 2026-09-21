/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.flow

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction

/**
 * Helper class that handles TabGroup flow SnackBar messages outside the TabsTray.
 *
 * @property appStore Store used to dispatch messages to the app-level SnackBar. The application level SnackBar is
 *   needed because this flow is destroyed upon user taking an action.
 */
class TabGroupFlowSnackBarMiddleware(val appStore: AppStore) : Middleware<TabGroupFlowState, TabGroupAction> {

    val logger = Logger("TabGroupSnackBarMiddleware")

    override fun invoke(
        store: Store<TabGroupFlowState, TabGroupAction>,
        next: (TabGroupAction) -> Unit,
        action: TabGroupAction,
    ) {
        handleTabGroupAction(store, action)
        next(action)
    }

    private fun handleTabGroupAction(store: Store<TabGroupFlowState, TabGroupAction>, action: TabGroupAction) {
        when (action) {
            is TabGroupAction.SaveClicked -> {
                val formState = store.state.formState ?: return
                when (store.state.entryPoint) {
                    is TabGroupFlowEntryPoint.MoveTabToGroup -> {
                        appStore.dispatch(AppAction.TabMovedToGroup(formState.name))
                    }
                    is TabGroupFlowEntryPoint.AddTabToGroup -> {
                        appStore.dispatch(AppAction.TabAddedToGroup(formState.name))
                    }
                    else -> {
                        logger.info("Invalid state from a save action. ")
                    }
                }
            }
            is TabGroupAction.TabAddedToExistingTabGroup -> {
                appStore.dispatch(AppAction.TabAddedToGroup(groupTitle = action.groupTitle))
            }
            is TabGroupAction.TabMovedToExistingTabGroup -> {
                appStore.dispatch(AppAction.TabMovedToGroup(groupTitle = action.groupTitle))
            }
            else -> {}
        }
    }
}
