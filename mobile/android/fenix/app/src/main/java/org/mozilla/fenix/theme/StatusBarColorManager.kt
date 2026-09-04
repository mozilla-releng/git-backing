/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.theme

import android.app.Activity
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentManager
import androidx.fragment.app.FragmentManager.FragmentLifecycleCallbacks
import androidx.navigation.fragment.NavHostFragment
import org.mozilla.fenix.browser.BrowserFragment
import org.mozilla.fenix.customtabs.ExternalAppBrowserFragment
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.utils.Settings

/**
 * Uses the [ThemeManager] to set the status bar color based on the current fragment.
 *
 * @param themeManager The [ThemeManager] to use for setting the status bar color.
 * @param activity The [Activity] to set the status bar color on.
 * @param settings The [Settings] used to read whether the tab strip is enabled.
 * @param tabStripStatusBarView View class that sets the status bar background with the tab strip
 * gradient when the tab strip is visible.
 */
class StatusBarColorManager(
    private val themeManager: ThemeManager,
    private val activity: Activity,
    private val settings: Settings,
    private val tabStripStatusBarView: TabStripStatusBarView,
) : FragmentLifecycleCallbacks() {

    override fun onFragmentResumed(fragmentManager: FragmentManager, fragment: Fragment) {
        if (fragment is NavHostFragment ||
            fragment is DialogFragment ||
            fragment is ExternalAppBrowserFragment
        ) {
            return
        }

        themeManager.applyStatusBarTheme(activity)
        updateTabStripGradient(fragment)
    }

    private fun updateTabStripGradient(fragment: Fragment) {
        when {
            // Don't show the gradient if tab strip is not enabled or private mode is enabled.
            !settings.isTabStripEnabled || themeManager.currentTheme.isPrivate -> {
                tabStripStatusBarView.hide()
            }

            // Both screens paint the same gradient behind the strip, so the status bar above it
            // matches rather than letting an edge-to-edge homepage wallpaper show through.
            fragment is BrowserFragment || fragment is HomeFragment -> {
                tabStripStatusBarView.show()
            }

            else -> {
                tabStripStatusBarView.hide()
            }
        }
    }
}
