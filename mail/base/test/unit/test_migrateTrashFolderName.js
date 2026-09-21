/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test clearing the trash folder paths that folder discovery used to write to
 * the trash_folder_name pref of IMAP servers. See bug 2048831.
 */

var { MailMigrator } = ChromeUtils.importESModule(
  "resource:///modules/MailMigrator.sys.mjs"
);
var { MailServices } = ChromeUtils.importESModule(
  "resource:///modules/MailServices.sys.mjs"
);

// [server type, path stored in trash_folder_name, expected path after the
// migration]
var gServers = [
  // The default path, which only folder discovery or the old account settings
  // UI would have stored.
  ["imap", "Trash", ""],
  // A path the user must have picked themselves.
  ["imap", "[Gmail]/Bin", "[Gmail]/Bin"],
  ["imap", "INBOX/Deleted", "INBOX/Deleted"],
  // Other server types are not touched.
  ["pop3", "Trash", "Trash"],
];

var gServerKeys = [];

add_setup(async () => {
  for (let i = 0; i < gServers.length; i++) {
    const server = MailServices.accounts.createIncomingServer(
      `user${i}`,
      `server${i}.invalid`,
      gServers[i][0]
    );
    gServerKeys.push(server.key);
    Services.prefs.setStringPref(
      `mail.server.${server.key}.trash_folder_name`,
      gServers[i][1]
    );
  }
});

add_task(async function testMigrateTrashFolderName() {
  Services.prefs.setIntPref("mail.ui-rdf.version", 65);
  MailMigrator._migrateUI();

  for (let i = 0; i < gServerKeys.length; i++) {
    Assert.equal(
      Services.prefs.getStringPref(
        `mail.server.${gServerKeys[i]}.trash_folder_name`,
        ""
      ),
      gServers[i][2],
      `trash folder path of ${gServers[i][0]} server ${i} should be as expected`
    );
  }
});
