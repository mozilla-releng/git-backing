/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.tabstrip

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Represents the colors for the tab strip.
 *
 * @property backgroundBrush The brush used to paint the tab strip background, either a gradient
 * or a [SolidColor].
 * @property tabItemBackgroundColors The background colors of the tab strip items.
 */
data class TabStripColors(
    val backgroundBrush: Brush,
    val tabItemBackgroundColors: TabColors,
) {

    /**
     * Represents the background colors of a tab item.
     *
     * @param activeColor The color to use when the tab is selected.
     * @param inactiveColor The color to use when the tab is not selected.
     */
    data class TabColors(
        private val activeColor: Color,
        private val inactiveColor: Color,
    ) {
        /**
         * Returns the appropriate background color based on the tab's active state.
         *
         * @param isActive Whether the tab is currently selected/active.
         * @return The active color if the tab is active, otherwise the inactive color.
         */
        fun get(isActive: Boolean) = if (isActive) activeColor else inactiveColor
    }

    companion object {

        /**
         * Returns the default [TabStripColors] instance, used on both the homepage and web tabs so
         * the strip looks the same on either.
         */
        @Composable
        fun default() = TabStripColors(
            backgroundBrush = FirefoxTheme.gradients.accentSubtle.brush,
            tabItemBackgroundColors = TabColors(
                activeColor = MaterialTheme.colorScheme.surface,
                inactiveColor = Color.Transparent,
            ),
        )
    }
}
