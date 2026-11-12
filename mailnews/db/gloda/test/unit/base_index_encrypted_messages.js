/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Test that the content of S/MIME and OpenPGP encrypted messages only gets
 * indexed when mailnews.database.global.indexer.examine_encrypted_parts is set.
 * The test files loading this one set up messageInjection.
 */

var { waitForGlodaIndexer } = ChromeUtils.importESModule(
  "resource://testing-common/gloda/GlodaTestHelper.sys.mjs"
);
var { Gloda } = ChromeUtils.importESModule(
  "resource:///modules/gloda/GlodaPublic.sys.mjs"
);
var { GlodaConstants } = ChromeUtils.importESModule(
  "resource:///modules/gloda/GlodaConstants.sys.mjs"
);
var { GlodaMsgIndexer } = ChromeUtils.importESModule(
  "resource:///modules/gloda/IndexMsg.sys.mjs"
);
var { SyntheticMessageSet } = ChromeUtils.importESModule(
  "resource://testing-common/mailnews/MessageGenerator.sys.mjs"
);
var { mailTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/mailnews/MailTestUtils.sys.mjs"
);
var { EnigmailSingletons } = ChromeUtils.importESModule(
  "chrome://openpgp/content/modules/singletons.sys.mjs"
);
var { OpenPGPTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/mail/OpenPGPTestUtils.sys.mjs"
);
var { SmimeUtils } = ChromeUtils.importESModule(
  "resource://testing-common/mailnews/SmimeUtils.sys.mjs"
);

const smimeDataDir = "../../../../test/data/smime/";
const openpgpDataDir = "../../../../../mail/test/browser/openpgp/data/";

const smimeFile = `${smimeDataDir}alice.dsig.SHA256.multipart.env.eml`;
const openpgpFile = `${openpgpDataDir}eml/signed-by-0xfbfcc82a015e7330-encrypted-to-0xf231550c4f47e38e.eml`;
const undecryptableFile = `${openpgpDataDir}eml/enc-to-carol@pgp.icu-revoked.eml`;
const smimeOpaqueSignedFile = `${smimeDataDir}alice.sig.SHA256.opaque.eml`;

const EXAMINE_ENCRYPTED_PREF =
  "mailnews.database.global.indexer.examine_encrypted_parts";

var messageInjection;

/**
 * A message read from a file, usable in a SyntheticMessageSet for injection.
 */
class FileMessage {
  /**
   * @param {string} text - The raw message.
   */
  constructor(text) {
    this.text = text;
    this.messageId = /^Message-ID:\s*<([^>]+)>/im.exec(text)?.[1];
    this.metaState = {};
    this.date = new Date();
  }

  toMessageString() {
    return this.text;
  }
}

/**
 * Load the keys needed to decrypt the test messages.
 *
 * @param {?string} [alicePassphrase] - If set, Alice's secret key is imported
 *   protected by this passphrase, instead of the automatic one.
 */
async function setupEncryptionKeys(alicePassphrase = null) {
  SmimeUtils.ensureNSS();
  SmimeUtils.loadPEMCertificate(
    do_get_file(`${smimeDataDir}TestCA.pem`),
    Ci.nsIX509Cert.CA_CERT
  );
  SmimeUtils.loadCertificateAndKey(
    do_get_file(`${smimeDataDir}Bob.p12`),
    "nss"
  );

  await OpenPGPTestUtils.initOpenPGP();
  await OpenPGPTestUtils.importPrivateKey(
    null,
    do_get_file(
      alicePassphrase
        ? `${openpgpDataDir}keys/alice@openpgp.example-0xf231550c4f47e38e-secret-with-pp.asc`
        : `${openpgpDataDir}keys/alice@openpgp.example-0xf231550c4f47e38e-secret.asc`
    ),
    OpenPGPTestUtils.ACCEPTANCE_PERSONAL,
    alicePassphrase,
    !!alicePassphrase
  );
  await OpenPGPTestUtils.importPublicKey(
    null,
    do_get_file(
      `${openpgpDataDir}keys/bob@openpgp.example-0xfbfcc82a015e7330-pub.asc`
    )
  );

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(EXAMINE_ENCRYPTED_PREF);
  });
}

/**
 * Create an S/MIME message that can't be decrypted.
 *
 * @param {string} [smimeType] - The smime-type parameter of the message, or
 *   nothing to leave it out.
 * @returns {FileMessage}
 */
function makeUndecryptableSmimeMessage(smimeType) {
  const params = smimeType ? `; smime-type=${smimeType}` : "";
  return new FileMessage(
    "From: Alice <alice@example.com>\r\n" +
      "To: Bob <bob@example.com>\r\n" +
      `Subject: Undecryptable ${smimeType}\r\n` +
      "Date: Thu, 12 Nov 2026 12:00:00 +0200\r\n" +
      `Message-ID: <undecryptable-${smimeType}@example.com>\r\n` +
      "MIME-Version: 1.0\r\n" +
      `Content-Type: application/pkcs7-mime; name=smime.p7m${params}\r\n` +
      "Content-Transfer-Encoding: base64\r\n" +
      "Content-Disposition: attachment; filename=smime.p7m\r\n" +
      "\r\n" +
      btoa("This is not a CMS message.") +
      "\r\n"
  );
}

/**
 * Inject messages into a new folder and wait for them to be indexed.
 *
 * @param {Array<string|FileMessage>} [files] - Paths of the messages to
 *   inject, or the messages themselves.
 * @returns {nsIMsgFolder} the folder the messages were indexed in.
 */
async function indexEncryptedMessages(files = [smimeFile, openpgpFile]) {
  const messages = [];
  for (const file of files) {
    messages.push(
      file instanceof FileMessage
        ? file
        : new FileMessage(await IOUtils.readUTF8(do_get_file(file).path))
    );
  }
  const folderHandle = await messageInjection.makeEmptyFolder();
  await messageInjection.addSetsToFolders(
    [folderHandle],
    [new SyntheticMessageSet(messages)]
  );
  await waitForGlodaIndexer();
  return messageInjection.getRealInjectionFolder(folderHandle);
}

/**
 * @param {GlodaQuery} query - The query to run.
 * @param {nsIMsgFolder} folder - Folder the matches should be in.
 * @returns {GlodaMessage[]} the matching messages in the given folder.
 */
async function queryMessages(query, folder) {
  const { promise, resolve } = Promise.withResolvers();
  query.getCollection({
    onItemsAdded() {},
    onItemsModified() {},
    onItemsRemoved() {},
    onQueryCompleted(collection) {
      resolve(collection.items);
    },
  });
  const items = await promise;
  return items.filter(item => item.folderMessage?.folder == folder);
}

/**
 * @param {string} text - Text to search for in the full-text index.
 * @param {nsIMsgFolder} folder - Folder the matches should be in.
 * @returns {GlodaMessage[]} the matching messages in the given folder.
 */
async function fulltextSearch(text, folder) {
  const query = Gloda.newQuery(GlodaConstants.NOUN_MESSAGE);
  query.fulltextMatches(text);
  return queryMessages(query, folder);
}

/**
 * @param {nsIMsgFolder} folder - Folder the matches should be in.
 * @returns {GlodaMessage[]} the messages in the given folder that had their
 *   decrypted content indexed.
 */
async function findDecrypted(folder) {
  const query = Gloda.newQuery(GlodaConstants.NOUN_MESSAGE);
  query.wasDecrypted(true);
  return queryMessages(query, folder);
}

/**
 * @param {nsIMsgFolder} folder - Folder to look the message up in.
 * @param {integer} index - Index of the message in the folder.
 * @returns {GlodaMessage} the gloda message for the message.
 */
async function getGlodaMessage(folder, index) {
  const msgHdr = mailTestUtils.getMsgHdrN(folder, index);
  const { promise, resolve } = Promise.withResolvers();
  Gloda.getMessageCollectionForHeader(msgHdr, {
    onItemsAdded() {},
    onItemsModified() {},
    onItemsRemoved() {},
    onQueryCompleted(collection) {
      resolve(collection.items[0]);
    },
  });
  return promise;
}

async function test_encrypted_content_not_indexed_by_default() {
  Services.prefs.setBoolPref(EXAMINE_ENCRYPTED_PREF, false);
  const folder = await indexEncryptedMessages();

  const smimeMsg = await getGlodaMessage(folder, 0);
  Assert.ok(smimeMsg.isEncrypted, "S/MIME message should be marked encrypted");
  Assert.ok(
    !smimeMsg.wasDecrypted,
    "S/MIME message should not be marked decrypted"
  );
  const openpgpMsg = await getGlodaMessage(folder, 1);
  Assert.ok(
    openpgpMsg.isEncrypted,
    "OpenPGP message should be marked encrypted"
  );
  Assert.ok(
    !openpgpMsg.wasDecrypted,
    "OpenPGP message should not be marked decrypted"
  );
  Assert.equal(
    openpgpMsg.subject,
    "...",
    "OpenPGP message should be indexed with the unprotected subject"
  );

  Assert.deepEqual(
    await fulltextSearch("message", folder),
    [],
    "S/MIME encrypted body should not be found"
  );
  Assert.deepEqual(
    await fulltextSearch("callaloo", folder),
    [],
    "OpenPGP encrypted body should not be found"
  );
  Assert.deepEqual(
    await findDecrypted(folder),
    [],
    "No message should be found as decrypted"
  );
}

async function test_encrypted_content_indexed_with_pref() {
  Services.prefs.setBoolPref(EXAMINE_ENCRYPTED_PREF, true);
  const folder = await indexEncryptedMessages();

  const smimeMatches = await fulltextSearch("message", folder);
  Assert.equal(smimeMatches.length, 1, "S/MIME encrypted body should be found");
  Assert.equal(
    smimeMatches[0].folderMessage.messageId,
    mailTestUtils.getMsgHdrN(folder, 0).messageId,
    "The S/MIME message should match"
  );

  const openpgpMatches = await fulltextSearch("callaloo", folder);
  Assert.equal(
    openpgpMatches.length,
    1,
    "OpenPGP encrypted body should be found"
  );
  Assert.equal(
    openpgpMatches[0].folderMessage.messageId,
    mailTestUtils.getMsgHdrN(folder, 1).messageId,
    "The OpenPGP message should match"
  );
  Assert.equal(
    openpgpMatches[0].subject,
    "Signed Encrypted",
    "OpenPGP message should be indexed with the protected subject"
  );

  const decrypted = await findDecrypted(folder);
  Assert.deepEqual(
    decrypted.map(msg => msg.folderMessage.messageId).sort(),
    [0, 1].map(i => mailTestUtils.getMsgHdrN(folder, i).messageId).sort(),
    "Both messages should be found as decrypted"
  );
  Assert.ok(
    decrypted.every(msg => msg.isEncrypted && msg.wasDecrypted),
    "Both messages should be marked encrypted and decrypted"
  );
}

async function test_undecryptable_not_marked_decrypted() {
  Services.prefs.setBoolPref(EXAMINE_ENCRYPTED_PREF, true);
  const folder = await indexEncryptedMessages([undecryptableFile]);

  const msg = await getGlodaMessage(folder, 0);
  Assert.ok(msg.isEncrypted, "Message should be marked encrypted");
  Assert.ok(!msg.wasDecrypted, "Message should not be marked decrypted");
  Assert.deepEqual(
    await findDecrypted(folder),
    [],
    "Message should not be found as decrypted"
  );
}

async function test_reindex_without_pref_clears_decrypted() {
  Services.prefs.setBoolPref(EXAMINE_ENCRYPTED_PREF, true);
  const folder = await indexEncryptedMessages();
  Assert.equal(
    (await findDecrypted(folder)).length,
    2,
    "Both messages should be found as decrypted"
  );

  Services.prefs.setBoolPref(EXAMINE_ENCRYPTED_PREF, false);
  GlodaMsgIndexer.indexMessages(
    [0, 1].map(i => [folder, mailTestUtils.getMsgHdrN(folder, i).messageKey])
  );
  await waitForGlodaIndexer();

  Assert.deepEqual(
    await findDecrypted(folder),
    [],
    "No message should be found as decrypted after reindexing"
  );
  Assert.ok(
    !(await getGlodaMessage(folder, 1)).wasDecrypted,
    "OpenPGP message should no longer be marked decrypted"
  );
  Assert.deepEqual(
    await fulltextSearch("callaloo", folder),
    [],
    "OpenPGP decrypted body should no longer be found"
  );
}

/**
 * Whether an S/MIME message is encrypted should come from its smime-type,
 * not from whether it could be decrypted.
 */
async function test_smime_encrypted_from_smime_type() {
  for (const examine of [false, true]) {
    Services.prefs.setBoolPref(EXAMINE_ENCRYPTED_PREF, examine);
    const folder = await indexEncryptedMessages([
      makeUndecryptableSmimeMessage("enveloped-data"),
      makeUndecryptableSmimeMessage(),
      smimeOpaqueSignedFile,
    ]);

    const enveloped = await getGlodaMessage(folder, 0);
    Assert.ok(
      enveloped.isEncrypted,
      `Undecryptable enveloped-data should be marked encrypted (pref ${examine})`
    );
    Assert.ok(
      !enveloped.wasDecrypted,
      `Undecryptable enveloped-data should not be marked decrypted (pref ${examine})`
    );
    const untyped = await getGlodaMessage(folder, 1);
    Assert.ok(
      untyped.isEncrypted,
      `Undecryptable S/MIME without smime-type should be marked encrypted (pref ${examine})`
    );
    Assert.ok(
      !untyped.wasDecrypted,
      `Undecryptable S/MIME without smime-type should not be marked decrypted (pref ${examine})`
    );
    Assert.ok(
      !(await getGlodaMessage(folder, 2)).isEncrypted,
      `Opaque signed S/MIME should not be marked encrypted (pref ${examine})`
    );
  }
}

/**
 * Indexing must not touch the decryption state OpenPGP keeps for the message
 * on display.
 */
async function test_indexing_keeps_displayed_message_state() {
  Services.prefs.setBoolPref(EXAMINE_ENCRYPTED_PREF, true);
  const lastMessage = EnigmailSingletons.lastDecryptedMessage;
  Object.assign(lastMessage, {
    lastMessageData: "displayed message data",
    lastMessageURI: { folder: "displayed-folder", msgNum: "1" },
    mimePartNumber: "1",
    lastStatus: { decryptedHeaders: null },
    gossip: ["displayed message gossip"],
  });
  const expected = structuredClone(lastMessage);
  EnigmailSingletons.lastMessageDecryptTime = Date.now();

  const folder = await indexEncryptedMessages();
  Assert.equal(
    (await findDecrypted(folder)).length,
    2,
    "Both messages should be found as decrypted"
  );
  Assert.deepEqual(
    structuredClone(lastMessage),
    expected,
    "The state of the message on display should be unchanged"
  );
  EnigmailSingletons.clearLastDecryptedMessage();
}

/* exported base_index_encrypted_messages_tests */
var base_index_encrypted_messages_tests = [
  test_encrypted_content_not_indexed_by_default,
  test_encrypted_content_indexed_with_pref,
  test_undecryptable_not_marked_decrypted,
  test_reindex_without_pref_clears_decrypted,
  test_indexing_keeps_displayed_message_state,
  test_smime_encrypted_from_smime_type,
];
