/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.store

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/**
 * Middleware that filters impression events if the impression has already been recorded in [MenuState]. This enforces
 * that an impression is recorded at most once per menu open.
 */
class ImpressionFilteringMiddleware : Middleware<MenuState, MenuAction> {

    override fun invoke(
        store: Store<MenuState, MenuAction>,
        next: (MenuAction) -> Unit,
        action: MenuAction,
    ) {
        // Prevent duplicate impressions
        if (action is MenuImpressionEvent && store.state.recordedImpressions.contains(action.impressionId)) {
            return
        } else {
            next(action)
        }
    }
}
