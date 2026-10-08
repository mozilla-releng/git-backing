/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import kotlin.test.assertEquals
import mozilla.components.feature.listentopage.ListenError
import mozilla.components.feature.listentopage.PlaybackPhase
import org.junit.Test

class PlaybackUiStateTest {
    @Test
    fun `Test that when content is playing and an error is present, the state is error`() {
        val state =
            getPlaybackUiState(
                error = ListenError.ContentUnavailable,
                phase = PlaybackPhase.Playing,
            )
        assertEquals(expected = PlaybackUiState.Error(ListenError.ContentUnavailable), actual = state)
    }

    @Test
    fun `Test that when content is playing and an error is not present, the state is playing`() {
        val state =
            getPlaybackUiState(
                error = null,
                phase = PlaybackPhase.Playing,
            )
        assertEquals(expected = PlaybackUiState.Playing, actual = state)
    }

    @Test
    fun `Test that when content is paused and an error is present, the state is error`() {
        val state =
            getPlaybackUiState(
                error = ListenError.ContentUnavailable,
                phase = PlaybackPhase.Paused,
            )
        assertEquals(expected = PlaybackUiState.Error(ListenError.ContentUnavailable), actual = state)
    }

    @Test
    fun `Test that when content is paused and an error is not present, the state is paused`() {
        val state =
            getPlaybackUiState(
                error = null,
                phase = PlaybackPhase.Paused,
            )
        assertEquals(expected = PlaybackUiState.Paused, actual = state)
    }

    @Test
    fun `Test that when content is buffering and an error is present, the state is error`() {
        val state =
            getPlaybackUiState(
                error = ListenError.ContentUnavailable,
                phase = PlaybackPhase.Buffering,
            )
        assertEquals(expected = PlaybackUiState.Error(ListenError.ContentUnavailable), actual = state)
    }

    @Test
    fun `Test that when content is buffering and an error is not present, the state is playing`() {
        val state =
            getPlaybackUiState(
                error = null,
                phase = PlaybackPhase.Buffering,
            )
        assertEquals(expected = PlaybackUiState.Playing, actual = state)
    }
}
