/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.modifier

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.LayoutBoundsHolder
import androidx.compose.ui.layout.layoutBounds
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ImpressionsTest {
    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `GIVEN an item visible from its first layout WHEN it never moves THEN report it once after the minimum duration`() {
        var impressions = 0
        composeTestRule.mainClock.autoAdvance = false
        composeTestRule.setContent {
            Box(Modifier.size(ROW_HEIGHT).onImpression { impressions++ })
        }

        composeTestRule.mainClock.advanceTimeBy(SHORTER_THAN_DURATION_MS)
        assertEquals(0, impressions)

        composeTestRule.mainClock.advanceTimeBy(REST_OF_DURATION_MS)
        assertEquals(1, impressions)
    }

    @Test
    fun `GIVEN an item less than the minimum fraction inside its viewport THEN don't report it`() {
        assertEquals(0, impressionsWithViewportOverlap(rowOffset = 80.dp))
    }

    @Test
    fun `GIVEN an item more than the minimum fraction inside its viewport THEN report it`() {
        assertEquals(1, impressionsWithViewportOverlap(rowOffset = 70.dp))
    }

    @Test
    fun `GIVEN an item covered by the window insets THEN don't report it`() {
        assertEquals(0, impressionsWithTopInset(rowOffset = 0.dp))
    }

    @Test
    fun `GIVEN an item outside the window insets THEN report it`() {
        assertEquals(1, impressionsWithTopInset(rowOffset = 150.dp))
    }

    /** Lays out a [ROW_HEIGHT] item at [rowOffset] next to a 100dp tall viewport, overlapping it vertically only. */
    private fun impressionsWithViewportOverlap(rowOffset: Dp): Int {
        var impressions = 0
        composeTestRule.setContent {
            val viewport = remember { LayoutBoundsHolder() }
            Box(Modifier.fillMaxSize()) {
                Box(Modifier.size(100.dp).layoutBounds(viewport))
                Box(
                    Modifier.offset(y = rowOffset).size(ROW_HEIGHT).onImpression(viewportBounds = viewport) {
                        impressions++
                    }
                )
            }
        }
        composeTestRule.mainClock.advanceTimeBy(LONGER_THAN_DURATION_MS)
        return impressions
    }

    /** Lays out a [ROW_HEIGHT] item at [rowOffset] in a window whose top 100dp are covered by insets. */
    private fun impressionsWithTopInset(rowOffset: Dp): Int {
        var impressions = 0
        composeTestRule.setContent {
            Box(Modifier.fillMaxSize()) {
                Box(
                    Modifier.offset(y = rowOffset).size(ROW_HEIGHT).onImpression(
                        windowInsets = WindowInsets(top = 100.dp)
                    ) {
                        impressions++
                    }
                )
            }
        }
        composeTestRule.mainClock.advanceTimeBy(LONGER_THAN_DURATION_MS)
        return impressions
    }

    private companion object {
        val ROW_HEIGHT = 50.dp
        const val SHORTER_THAN_DURATION_MS = 300L
        const val REST_OF_DURATION_MS = 300L
        const val LONGER_THAN_DURATION_MS = 600L
    }
}
