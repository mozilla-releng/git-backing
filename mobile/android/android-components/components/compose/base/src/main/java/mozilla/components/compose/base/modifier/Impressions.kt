/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.modifier

import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.LayoutBoundsHolder
import androidx.compose.ui.layout.registerOnLayoutRectChanged
import androidx.compose.ui.node.CompositionLocalConsumerModifierNode
import androidx.compose.ui.node.DelegatableNode
import androidx.compose.ui.node.ModifierNodeElement
import androidx.compose.ui.node.ObserverModifierNode
import androidx.compose.ui.node.UnplacedAwareModifierNode
import androidx.compose.ui.node.currentValueOf
import androidx.compose.ui.node.observeReads
import androidx.compose.ui.platform.InspectorInfo
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.unit.IntRect
import kotlin.time.Duration.Companion.milliseconds
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * A modifier that will call onShown() whenever an item meets the passed in threshold criteria. Will call for each
 * impression count.
 *
 * @param threshold The duration and fraction visible for an item to be considered visible.
 * @param viewportBounds Optionally, the bounds of the viewport to consider.
 * @param windowInsets Optionally, the WindowInsets to consider.
 * @param onShown The lambda executed when the threshold criteria is met.
 */
fun Modifier.onImpression(
    threshold: ImpressionThreshold = ImpressionThreshold.DefaultThreshold,
    viewportBounds: LayoutBoundsHolder? = null,
    windowInsets: WindowInsets? = null,
    onShown: () -> Unit,
): Modifier {
    return this then
        ImpressionElement(
            windowInsets = windowInsets,
            threshold = threshold,
            viewportBounds = viewportBounds,
            onShown = onShown,
        )
}

/**
 * Data class that wraps an ImpressionNode so it may be used in a modifier.
 *
 * @param threshold The duration and fraction visible for an item to be considered visible.
 * @param viewportBounds Optionally, the bounds of the viewport to consider.
 * @param windowInsets Optionally, the WindowInsets to consider.
 * @param onShown The lambda executed when the threshold criteria is met.
 */
private data class ImpressionElement(
    val threshold: ImpressionThreshold,
    val viewportBounds: LayoutBoundsHolder?,
    val windowInsets: WindowInsets?,
    val onShown: () -> Unit,
) : ModifierNodeElement<ImpressionNode>() {
    override fun create() =
        ImpressionNode(
            threshold = threshold,
            viewportBounds = viewportBounds,
            onShown = onShown,
            windowInsets = windowInsets,
        )

    override fun update(node: ImpressionNode) {
        node.update(threshold, viewportBounds, windowInsets, onShown)
    }

    override fun InspectorInfo.inspectableProperties() {
        name = "onImpression"
        properties["minFractionVisible"] = threshold.minFractionVisible
        properties["minDurationMs"] = threshold.minDurationMs
        properties["viewportBounds"] = viewportBounds
        properties["windowInsets"] = windowInsets
    }
}

/**
 * Modifier.Node that tracks whether a given node is visible within the intersection of its row and viewport up to a
 * given threshold of fraction visible, for a given time. Will call onShown() when this is the case.
 *
 * @property threshold The duration and fraction visible for an item to be considered visible.
 * @property viewportBounds Optionally, the bounds of the viewport to consider.
 * @property windowInsets Optionally, the WindowInsets to consider.
 * @property onShown The lambda executed when the threshold criteria is met.
 */
private class ImpressionNode(
    var threshold: ImpressionThreshold,
    var viewportBounds: LayoutBoundsHolder?,
    private var windowInsets: WindowInsets? = null,
    var onShown: () -> Unit,
) : Modifier.Node(), CompositionLocalConsumerModifierNode, ObserverModifierNode, UnplacedAwareModifierNode {
    private var registration: DelegatableNode.RegistrationHandle? = null
    private var rowBounds: IntRect? = null
    private var qualifies = false
    private var visibleTimerJob: Job? = null

    override fun onAttach() {
        registration =
            registerOnLayoutRectChanged(
                throttleMillis = 0,
                debounceMillis = 0,
            ) { bounds ->
                rowBounds = bounds.boundsInWindow
                updateVisibility()
            }
    }

    override fun onObservedReadsChanged() = updateVisibility()

    override fun onUnplaced() = clearPlacement()

    override fun onDetach() {
        registration?.unregister()
        registration = null
        clearPlacement()
    }

    private fun updateVisibility() {
        var visible = false
        observeReads {
            visible = meetsThreshold()
        }
        if (visible == qualifies) return
        if (!visible) {
            cancelTimer()
            return
        }
        qualifies = true
        if (threshold.minDurationMs == 0L) {
            onShown()
            return
        }
        visibleTimerJob = coroutineScope.launch {
            delay(threshold.minDurationMs.milliseconds)
            visibleTimerJob = null
            // This recheck guards against the row and viewport bounds being briefly out of sync.
            if (meetsThreshold()) {
                onShown()
            } else {
                cancelTimer()
            }
        }
    }

    private fun meetsThreshold(): Boolean {
        val row = rowBounds ?: return false
        if (row.width <= 0 || row.height <= 0) {
            return false
        }
        val window = windowRect()
        val viewport =
            if (viewportBounds == null) {
                window
            } else {
                viewportBounds?.bounds?.boundsInWindow ?: return false
            }
        val visible = row.intersect(viewport).intersect(window)
        if (visible.width <= 0 || visible.height <= 0) return false
        val visibleArea = visible.width.toDouble() * visible.height
        val rowArea = row.width.toDouble() * row.height
        return visibleArea / rowArea >= threshold.minFractionVisible
    }

    private fun windowRect(): IntRect {
        val size = currentValueOf(LocalWindowInfo).containerSize
        val insets = windowInsets ?: return IntRect(left = 0, top = 0, right = size.width, bottom = size.height)
        val density = currentValueOf(LocalDensity)
        val direction = currentValueOf(LocalLayoutDirection)

        return IntRect(
            left = insets.getLeft(density = density, layoutDirection = direction),
            top = insets.getTop(density = density),
            right = size.width - insets.getRight(density = density, layoutDirection = direction),
            bottom = size.height - insets.getBottom(density = density),
        )
    }

    private fun cancelTimer() {
        qualifies = false
        visibleTimerJob?.cancel()
        visibleTimerJob = null
    }

    private fun clearPlacement() {
        rowBounds = null
        cancelTimer()
    }

    override fun onReset() {
        clearPlacement()
    }

    fun update(
        threshold: ImpressionThreshold,
        viewportBounds: LayoutBoundsHolder?,
        windowInsets: WindowInsets?,
        onShown: () -> Unit,
    ) {
        this.onShown = onShown

        if (
            this.threshold == threshold && this.viewportBounds === viewportBounds && this.windowInsets === windowInsets
        ) {
            return
        }

        this.threshold = threshold
        this.viewportBounds = viewportBounds
        this.windowInsets = windowInsets

        cancelTimer()
        if (isAttached) updateVisibility()
    }

    override val shouldAutoInvalidate: Boolean = false
}
