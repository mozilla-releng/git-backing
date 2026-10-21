/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.downloads.listscreen.middleware

import androidx.test.core.app.ApplicationProvider
import kotlin.test.assertEquals
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.state.content.DownloadState.Status
import mozilla.components.feature.downloads.DefaultFileSizeFormatter
import mozilla.components.feature.downloads.DownloadEstimator
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import mozilla.components.support.utils.FakeDateTimeProvider
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.helpers.ShadowInetAddresses
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(shadows = [ShadowInetAddresses::class])
class FileItemDescriptionProviderTest {
    private val fakeDateTimeProvider = FakeDateTimeProvider()
    private val target =
        DefaultFileItemDescriptionProvider(
            context = ApplicationProvider.getApplicationContext(),
            fileSizeFormatter = DefaultFileSizeFormatter(context = ApplicationProvider.getApplicationContext()),
            downloadEstimator = DownloadEstimator(dateTimeProvider = fakeDateTimeProvider),
            publicSuffixList = PublicSuffixList(context = ApplicationProvider.getApplicationContext()),
        )
    private val path = "mozilla.org/index/downloads/download.pdf"

    @Test
    fun `GIVEN download is completed and scheme is https THEN description is a short url`() = runTest {
        val uri = "https://www.$path"
        assertEquals(
            actual =
                target.getDescription(
                    downloadState =
                        DownloadState(
                            url = uri,
                            status = Status.COMPLETED,
                        )
                ),
            expected = "0 B • mozilla.org",
        )
    }

    @Test
    fun `GIVEN download is completed and scheme is data THEN description is data scheme`() = runTest {
        val uri = "data://$path"
        assertEquals(
            actual =
                target.getDescription(
                    downloadState =
                        DownloadState(
                            url = uri,
                            status = Status.COMPLETED,
                        )
                ),
            expected = "0 B • (data)",
        )
    }

    @Test
    fun `GIVEN a data prefix appended with a trusted domain, THEN description is data scheme`() = runTest {
        val trustedDomain = "www.firefox.com/"
        val injectedFalsePath = ".https://firefox.com/a/b/c/test/"
        val uri = "data:application/octet-stream;base-64,ZXN0dGVzdGVzdHRlc3Q=#${trustedDomain + injectedFalsePath}"
        assertEquals(
            actual =
                target.getDescription(
                    downloadState =
                        DownloadState(
                            url = uri,
                            status = Status.COMPLETED,
                        )
                ),
            expected = "0 B • (data)",
        )
    }

    @Test
    fun `GIVEN an empty string, THEN description is 0 B`() = runTest {
        assertEquals(
            actual =
                target.getDescription(
                    downloadState =
                        DownloadState(
                            url = "",
                            status = Status.COMPLETED,
                        )
                ),
            expected = "0 B",
        )
    }

    @Test
    fun `GIVEN a data URL download with appended trusted domain WHEN mapped THEN search url shows the data origin`() =
        runTest {
            assertEquals(
                actual =
                    target.getDescription(
                        downloadState =
                            DownloadState(
                                url =
                                    "data:application/octet-stream;base-64,ZXN0dGVzdGVzdHRlc3Q=#www.mozilla.org/.https://www.mozilla.org/downloads/test.pdf",
                                status = Status.COMPLETED,
                            )
                    ),
                expected = "0 B • (data)",
            )
        }
}
