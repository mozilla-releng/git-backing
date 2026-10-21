/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ext

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertEquals
import kotlinx.coroutines.test.runTest
import mozilla.components.lib.publicsuffixlist.PublicSuffixList
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R

// Robolectric's InetAddresses shadow resolves hostnames via DNS.  This test needs the real (Android) check.
@RunWith(AndroidJUnit4::class)
class DownloadDisplayTest {
    private val publicSuffixList: PublicSuffixList = PublicSuffixList(ApplicationProvider.getApplicationContext())
    private val dataLabel =
        ApplicationProvider.getApplicationContext<Context>().getString(R.string.download_item_source_data)

    @Test
    fun toDownloadDisplayUrl() = runTest {
        val testCases =
            listOf(
                "https://prod-1.storage.jamendo.com/?trackid=2233894&format=mp35&download=true" to "jamendo.com",
                "blob:https://www.pinterest.com/98752e42-2707-44b4-81e3-161a04f0d3aa" to "pinterest.com",
                "https://mozilla.org" to "mozilla.org",
                "http://wikipedia.org" to "wikipedia.org",
                "http://plus.google.com" to "google.com",
                "https://en.m.wikipedia.org/wiki/Main_Page" to "wikipedia.org",
                "https://www.mozilla.org@evil.com/x" to "evil.com",
                "https://xn--mozlla-7we.org/f" to "xn--mozlla-7we.org",
                "data:application/octet-stream;base64;ZN432903945xj=#.https://www.mozilla.com/a/b/c" to dataLabel,
                "firefox.com/update//update" to "",
                "data://www.firefox.com/,payload" to dataLabel,
                "data:text/html,42" to dataLabel,
                "https://192.168.1.10/f" to "192.168.1.10",
                "https://[::1]/f" to "[::1]",
                "HTTPS://WWW.MOZILLA.ORG/" to "",
                "ftp://example.com/f" to "",
                "moz-extension://uuid/f" to "",
                "blob:null/uuid" to dataLabel,
                "http://localhost/f" to "localhost",
                "https://www.example.co.uk/" to "example.co.uk",
                "https://webmail.example.co.uk" to "example.co.uk",
                "https://www.com" to "www.com",
                "https://www.co.uk" to "www.co.uk",
                "http://www.city.mikasa.hokkaido.jp/" to "city.mikasa.hokkaido.jp",
                "https://subdomain.example.com:8443/" to "example.com",
                "mailto:example@subdomain.example.com" to "",
                "https://subdomain.bäcker.de/" to "bäcker.de",
                "https://subdomain.bäcker.de:8443/" to "bäcker.de",
                "" to "",
            )

        testCases.forEach { (raw, expected) ->
            val actual = raw.toDownloadDisplayUrl(publicSuffixList, dataLabel)
            assertEquals(expected = expected, actual = actual, message = "Expected $expected from $raw but was $actual")
        }
    }
}
