/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Test that indexing encrypted messages does not prompt for the passphrase
 * of an OpenPGP key protected by a user-set passphrase.
 */

var { glodaTestHelperInitialize } = ChromeUtils.importESModule(
  "resource://testing-common/gloda/GlodaTestHelper.sys.mjs"
);
var { MessageInjection } = ChromeUtils.importESModule(
  "resource://testing-common/mailnews/MessageInjection.sys.mjs"
);

/* import-globals-from base_index_encrypted_messages.js */
load("base_index_encrypted_messages.js");

const ALICE_PASSPHRASE = "alice-passphrase";

const gPromptService = {
  promptCount: 0,
  promptPassword(parent, dialogTitle, text, password) {
    this.promptCount++;
    password.value = ALICE_PASSPHRASE;
    return true;
  },
};

add_setup(async function () {
  const originalPromptService = Services.prompt;
  Services.prompt = gPromptService;
  registerCleanupFunction(() => {
    Services.prompt = originalPromptService;
  });
  await setupEncryptionKeys(ALICE_PASSPHRASE);
  messageInjection = new MessageInjection({ mode: "local" });
  glodaTestHelperInitialize(messageInjection);
});

add_task(async function test_no_passphrase_prompt() {
  Services.prefs.setBoolPref(EXAMINE_ENCRYPTED_PREF, true);
  const folder = await indexEncryptedMessages();

  Assert.equal(
    gPromptService.promptCount,
    0,
    "Indexing should not prompt for a passphrase"
  );

  const smimeMsg = await getGlodaMessage(folder, 0);
  Assert.ok(smimeMsg.wasDecrypted, "S/MIME message should be decrypted");

  const openpgpMsg = await getGlodaMessage(folder, 1);
  Assert.ok(
    openpgpMsg.isEncrypted,
    "OpenPGP message should be marked encrypted"
  );
  Assert.ok(
    !openpgpMsg.wasDecrypted,
    "OpenPGP message should not be decrypted without the passphrase"
  );
  Assert.deepEqual(
    await fulltextSearch("callaloo", folder),
    [],
    "OpenPGP encrypted body should not be found"
  );
});
