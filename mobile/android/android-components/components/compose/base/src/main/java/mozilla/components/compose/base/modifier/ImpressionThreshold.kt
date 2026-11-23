/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.modifier

/**
 * Data class defining impression thresholds.
 *
 * @property minDurationMs The time in ms an item must be visible for to count as an impression. May be 0.
 * @property minFractionVisible The fraction as a float that an item must be visible for it to count as an impression.
 *   Must be a positive float.
 */
data class ImpressionThreshold(
    val minDurationMs: Long,
    val minFractionVisible: Float,
) {
    companion object {
        /** The default threshold for an impression if not otherwise configured. Guidance from UX. */
        val DefaultThreshold: ImpressionThreshold =
            ImpressionThreshold(
                minDurationMs = 500L,
                minFractionVisible = 0.5f,
            )
    }
}
