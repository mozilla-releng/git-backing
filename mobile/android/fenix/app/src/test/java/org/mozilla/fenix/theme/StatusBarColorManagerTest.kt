/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.theme

import android.app.Activity
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentManager
import androidx.navigation.fragment.NavHostFragment
import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import org.junit.Test
import org.mozilla.fenix.browser.BrowserFragment
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.customtabs.ExternalAppBrowserFragment
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.utils.Settings

class StatusBarColorManagerTest {

    private val themeManager: ThemeManager = mockk(relaxed = true) {
        every { currentTheme } returns BrowsingMode.Normal
    }
    private val activity: Activity = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)
    private val tabStripStatusBarView: TabStripStatusBarView = mockk(relaxed = true)
    private val fragmentManager: FragmentManager = mockk(relaxed = true)
    private val homeFragment: HomeFragment = mockk(relaxed = true)

    @Test
    fun `GIVEN tab strip is enabled WHEN the home fragment resumes THEN the gradient is shown`() {
        val manager = buildStatusBarColorManager(isTabStripEnabled = true)
        manager.onFragmentResumed(fragmentManager, homeFragment)

        verify {
            themeManager.applyStatusBarTheme(activity)
            tabStripStatusBarView.show()
        }
    }

    @Test
    fun `GIVEN tab strip is enabled WHEN the browser fragment resumes THEN the gradient is shown`() {
        val manager = buildStatusBarColorManager(isTabStripEnabled = true)
        manager.onFragmentResumed(fragmentManager, mockk<BrowserFragment>())

        verify { tabStripStatusBarView.show() }
    }

    @Test
    fun `GIVEN tab strip is enabled WHEN a fragment without a tab strip resumes THEN the gradient is hidden`() {
        val manager = buildStatusBarColorManager(isTabStripEnabled = true)
        manager.onFragmentResumed(fragmentManager, mockk<Fragment>())

        verify {
            themeManager.applyStatusBarTheme(activity)
            tabStripStatusBarView.hide()
        }
    }

    @Test
    fun `GIVEN private browsing mode is enabled WHEN a tab strip fragment resumes THEN the gradient is hidden`() {
        every { themeManager.currentTheme } returns BrowsingMode.Private

        val manager = buildStatusBarColorManager(isTabStripEnabled = true)
        manager.onFragmentResumed(fragmentManager, homeFragment)

        verify {
            themeManager.applyStatusBarTheme(activity)
            tabStripStatusBarView.hide()
        }
    }

    @Test
    fun `GIVEN tab strip is disabled WHEN a tab strip fragment resumes THEN the gradient is hidden`() {
        val manager = buildStatusBarColorManager(isTabStripEnabled = false)
        manager.onFragmentResumed(fragmentManager, homeFragment)

        verify {
            themeManager.applyStatusBarTheme(activity)
            tabStripStatusBarView.hide()
        }
    }

    @Test
    fun `GIVEN NavHostFragment, DialogFragment and ExternalAppBrowserFragment as ignored fragments WHEN an ignored fragment resumes THEN do nothing`() {
        val manager = buildStatusBarColorManager(isTabStripEnabled = true)

        manager.onFragmentResumed(fragmentManager, mockk<NavHostFragment>())
        manager.onFragmentResumed(fragmentManager, mockk<DialogFragment>())
        manager.onFragmentResumed(fragmentManager, mockk<ExternalAppBrowserFragment>())

        verify(exactly = 0) {
            themeManager.applyStatusBarTheme(any())
            tabStripStatusBarView.show()
            tabStripStatusBarView.hide()
        }
    }

    private fun buildStatusBarColorManager(isTabStripEnabled: Boolean): StatusBarColorManager {
        every { settings.isTabStripEnabled } returns isTabStripEnabled

        return StatusBarColorManager(
            themeManager = themeManager,
            activity = activity,
            settings = settings,
            tabStripStatusBarView = tabStripStatusBarView,
        )
    }
}
