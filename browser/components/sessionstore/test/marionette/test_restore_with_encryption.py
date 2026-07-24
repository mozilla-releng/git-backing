# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys

sys.path.append(os.path.dirname(__file__))

from session_store_test_case import SessionStoreTestCase


class TestSessionRestoreWithEncryption(SessionStoreTestCase):
    def setUp(self):
        super().setUp(startup_page=3, include_private=False)
        # Enable encryption after setUp opened windows. set_pref
        # changes the pref at runtime without a restart, so the
        # session data written from this point on is encrypted.
        self.marionette.set_pref("browser.sessionstore.encryption.enabled", True)

    def test_restore_with_encryption(self):
        """Encrypted session data should be restored after a restart."""
        self.wait_for_windows(
            self.test_windows, "Not all requested windows have been opened"
        )

        self.marionette.quit()
        self.marionette.start_session()
        self.marionette.set_context("chrome")

        self.wait_for_windows(
            self.test_windows,
            "Windows should be restored from encrypted session data",
        )
