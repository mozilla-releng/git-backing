/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ext

import androidx.test.core.app.ApplicationProvider
import kotlin.test.assertEquals
import kotlinx.coroutines.test.runTest
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class StringTest {
    private val publicSuffixList: PublicSuffixList = PublicSuffixList(ApplicationProvider.getApplicationContext())

    @Test
    fun `Simplified Url`() {
        val urlTest = "https://www.amazon.com"
        val new = urlTest.simplifiedUrl()
        assertEquals(expected = new, actual = "amazon.com")
    }

    @Test
    fun testReplaceConsecutiveZeros() {
        assertEquals(
            expected = "2001:db8::ff00:42:8329",
            actual = "2001:db8:0:0:0:ff00:42:8329".replaceConsecutiveZeros(),
        )
    }

    @Test
    fun getBaseDomainUrl() = runTest {
        val testCases =
            listOf(
                "https://prod-1.storage.jamendo.com/?trackid=2233894&format=mp35&download=true" to "jamendo.com",
                "blob:https://www.pinterest.com/98752e42-2707-44b4-81e3-161a04f0d3aa" to "pinterest.com",
                "https://mozilla.org" to "mozilla.org",
                "http://wikipedia.org" to "wikipedia.org",
                "http://plus.google.com" to "google.com",
                "https://en.m.wikipedia.org/wiki/Main_Page" to "wikipedia.org",
            )

        testCases.forEach { (raw, expected) ->
            assertEquals(expected = expected, actual = raw.getBaseDomainUrl(publicSuffixList))
        }
    }
}
