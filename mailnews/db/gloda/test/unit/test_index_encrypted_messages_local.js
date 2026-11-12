/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Test indexing of encrypted local messages.
 */

var { glodaTestHelperInitialize } = ChromeUtils.importESModule(
  "resource://testing-common/gloda/GlodaTestHelper.sys.mjs"
);
var { MessageInjection } = ChromeUtils.importESModule(
  "resource://testing-common/mailnews/MessageInjection.sys.mjs"
);

/* import-globals-from base_index_encrypted_messages.js */
load("base_index_encrypted_messages.js");

add_setup(async function () {
  await setupEncryptionKeys();
  messageInjection = new MessageInjection({ mode: "local" });
  glodaTestHelperInitialize(messageInjection);
});

base_index_encrypted_messages_tests.forEach(test => {
  add_task(test);
});
