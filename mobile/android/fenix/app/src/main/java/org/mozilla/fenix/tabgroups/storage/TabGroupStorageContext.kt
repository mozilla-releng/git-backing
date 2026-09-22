/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.fenix.tabgroups.storage

import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.state.TabGroupFormState

/**
 * Context interface wrapper that allows different state objects to serve as the context for invoking TabGroup storage
 * actions.
 */
interface TabGroupStorageContext {
    val groups: List<TabsTrayItem.TabGroup>
    val formState: TabGroupFormState?
}
