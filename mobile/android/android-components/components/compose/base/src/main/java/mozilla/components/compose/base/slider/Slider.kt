/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.slider

import androidx.compose.animation.core.animateDpAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.DragInteraction
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsDraggedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.layout
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.offset
import mozilla.components.compose.base.theme.AcornShapes
import mozilla.components.compose.base.theme.AcornTheme

private const val START_VALUE = 0f
private const val END_VALUE = 1f

/**
 * Acorn component for M3 slider. Implements an oblong thumb button with an expanding animation for the button and track
 * during a scrub action, and a track that shows the filled and unfilled area with Acorn theming.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun Slider(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    start: Float = START_VALUE,
    end: Float = END_VALUE,
    isEnabled: Boolean = true,
    onValueChangeFinished: () -> Unit = {},
    interactionSource: MutableInteractionSource = remember { MutableInteractionSource() },
) {
    val isDragInProgress by interactionSource.collectIsDraggedAsState()
    val thumbWidth by
    animateDpAsState(if (isDragInProgress) AcornTheme.layout.size.static400 else AcornTheme.layout.size.static200)
    Slider(
        value = value,
        onValueChange = { newInput ->
            onValueChange(newInput)
        },
        valueRange = start..end,
        modifier = modifier.extendTrackToEdges(thumbWidth),
        enabled = isEnabled,
        onValueChangeFinished = {
            onValueChangeFinished()
        },
        interactionSource = interactionSource,
        thumb = { Thumb(isEnabled = isEnabled, isDragInProgress = isDragInProgress, width=thumbWidth) },
        track = { _ ->
            // Calculate fraction of the slider that is active
            // remembering this doesn't save anything since it has to be recalculated on any value update
            val fraction = activeTrackFraction(value = value, start = start, end = end)
            Track(fraction = fraction, isDragInProgress = isDragInProgress, isEnabled = isEnabled)
        },
    )
}


/**
 * Widens a slider by [thumbWidth] and places it in the center.
 * By default, M3 insets the track by half a thumb on each side so that the thumb at rest is in line with the edge of
 * the component. This causes the track to line up with the edge of the space exactly instead of leaving a gap, and the
 * thumb to overhang the edge.
 */
private fun Modifier.extendTrackToEdges(thumbWidth: Dp): Modifier = layout { measurable, constraints ->
    val extra = thumbWidth.roundToPx()
    val placeable = measurable.measure(constraints.offset(horizontal = extra))
    layout((placeable.width - extra).coerceAtLeast(0), placeable.height) { placeable.placeRelative(-extra / 2, 0) }
}

/** How much of the track between [start] and [end] is filled at [value], kept within 0 to 1. */
internal fun activeTrackFraction(value: Float, start: Float, end: Float): Float =
    if (end > start) ((value - start) / (end - start)).coerceIn(0f, 1f) else 0f

/** Thumb is the draggable handle of the slider that user moves to change the value. */
@Composable
private fun Thumb(isEnabled: Boolean, isDragInProgress: Boolean, width: Dp) {
    val color =
        if (isEnabled) {
            MaterialTheme.colorScheme.primary
        } else {
            MaterialTheme.colorScheme.surfaceContainerHighest
        }
    val height by
        animateDpAsState(if (isDragInProgress) AcornTheme.layout.size.static300 else AcornTheme.layout.size.static150)
    Box(
        modifier =
            Modifier.padding(vertical = AcornTheme.layout.space.static100)
                .size(
                    width = AcornTheme.layout.size.static200,
                    height = AcornTheme.layout.size.static150,
                )
                // M3 slider measures the track as slider width - thumb width,
                // so a slider that expands on drag needs a separate renderable size
                // and drawable size
                .requiredSize(width = width, height = height)
                .background(color = color, shape = AcornShapes.large)
    )
}

@Composable
private fun Track(fraction: Float, isDragInProgress: Boolean, isEnabled: Boolean) {
    Box(
        contentAlignment = Alignment.CenterStart,
        modifier = Modifier.fillMaxWidth(),
    ) {
        EmptyTrack()
        FilledTrack(fraction = fraction, isDragInProgress = isDragInProgress, isEnabled = isEnabled)
    }
}

@Composable
private fun EmptyTrack() {
    Box(
        modifier =
            Modifier.fillMaxWidth()
                .height(AcornTheme.layout.size.static50)
                .background(
                    color = MaterialTheme.colorScheme.surfaceContainerHighest,
                    shape = MaterialTheme.shapes.medium,
                )
    ) {}
}

@Composable
private fun FilledTrack(fraction: Float, isDragInProgress: Boolean, isEnabled: Boolean) {
    val color = if (isEnabled) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surfaceContainerHighest
    val height =
        animateDpAsState(
            (if (isDragInProgress) {
                AcornTheme.layout.size.static100
            } else {
                AcornTheme.layout.size.static50
            })
        )
    Box(
        modifier =
            Modifier.fillMaxWidth(fraction)
                .height(height.value)
                .background(
                    color = color,
                    shape = MaterialTheme.shapes.medium,
                )
    ) {}
}

// Use this preview for interactive testing.
@PreviewLightDark
@Composable
private fun SliderPreview() {
    var value by remember { mutableFloatStateOf(.5f) }
    AcornTheme {
        Box(modifier = Modifier.width(300.dp).background(MaterialTheme.colorScheme.surface)) {
            Slider(
                isEnabled = true,
                value = value,
                onValueChange = { value = it },
                onValueChangeFinished = {},
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun SliderDisabledPreview() {
    AcornTheme {
        Box(modifier = Modifier.width(300.dp).background(MaterialTheme.colorScheme.surface)) {
            Slider(
                isEnabled = false,
                value = .4f,
                onValueChange = {},
                onValueChangeFinished = {},
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun SliderDraggingInProgressPreview() {
    val interactionSource = remember { MutableInteractionSource() }
    AcornTheme {
        Box(modifier = Modifier.width(300.dp).background(MaterialTheme.colorScheme.surface)) {
            Slider(
                isEnabled = true,
                value = .5f,
                onValueChange = {},
                onValueChangeFinished = {},
                interactionSource = interactionSource,
            )
        }
    }
    LaunchedEffect(Unit) {
        interactionSource.emit(DragInteraction.Start())
    }
}
