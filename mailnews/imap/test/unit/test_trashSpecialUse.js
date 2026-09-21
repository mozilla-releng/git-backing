/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Tests that the folder the server reports as \Trash special-use is used as
 * the trash folder, whatever it is named and whether or not it is subscribed.
 * See bug 2048831: Gmail accounts using English (UK) name it "Bin".
 */

const { IMAPServer } = ChromeUtils.importESModule(
  "resource://testing-common/mailnews/IMAPServer.sys.mjs"
);

/**
 * @param {object} options
 * @param {string} options.trashName - Name of the \Trash folder.
 * @param {boolean} options.subscribed - Whether the \Trash folder is
 *   subscribed. Gmail leaves some of its folders unsubscribed.
 * @param {string} [options.prefix] - Folder prefix the \Trash folder lives
 *   under, e.g. "[Google Mail]", or none for a top level folder.
 * @param {boolean} [options.strayTrash] - Add a folder named "Trash" with no
 *   special-use flag, as created by earlier Thunderbird versions.
 * @returns {IMAPServer}
 */
function createServer({ trashName, subscribed, prefix, strayTrash }) {
  const server = new IMAPServer({
    extensions: ["GMAIL", "RFC2197", "RFC2342", "RFC3348", "RFC4315"],
  });
  server.daemon.getMailbox("INBOX").specialUseFlag = "\\Inbox";
  server.daemon.getMailbox("INBOX").subscribed = true;
  if (strayTrash) {
    server.daemon.createMailbox("Trash", { subscribed: true });
  }
  if (prefix) {
    server.daemon.createMailbox(prefix, {
      flags: ["\\NoSelect"],
      subscribed: true,
    });
    server.daemon.createMailbox(`${prefix}/All Mail`, {
      flags: ["\\Archive"],
      specialUseFlag: "\\AllMail",
      subscribed,
    });
    server.daemon.createMailbox(`${prefix}/Sent Mail`, {
      flags: ["\\Sent"],
      subscribed: true,
    });
    server.daemon.createMailbox(`${prefix}/Spam`, {
      flags: ["\\Spam"],
      subscribed: true,
    });
  }
  server.daemon.createMailbox(prefix ? `${prefix}/${trashName}` : trashName, {
    flags: ["\\Trash"],
    subscribed,
  });
  return server;
}

async function createAccount(server, prefPath) {
  const account = MailServices.accounts.createAccount();
  account.addIdentity(MailServices.accounts.createIdentity());
  account.incomingServer = MailServices.accounts.createIncomingServer(
    "user",
    "localhost",
    "imap"
  );
  account.incomingServer.password = "password";
  account.incomingServer.port = server.port;
  if (prefPath) {
    Services.prefs.setStringPref(
      `mail.server.${account.incomingServer.key}.trash_folder_name`,
      prefPath
    );
  }
  await discoverFolders(account);
  return account;
}

async function discoverFolders(account) {
  const listener = new PromiseTestUtils.PromiseUrlListener();
  MailServices.imap.discoverAllFolders(
    account.incomingServer.rootFolder,
    listener,
    null
  );
  await listener.promise;
}

/**
 * Discover the folders of a server and check that the folder at `path` is the
 * one used as trash.
 *
 * @param {IMAPServer} server
 * @param {string} path - Online path of the expected trash folder.
 * @param {object} [options]
 * @param {boolean} [options.strayTrash] - The server has a folder named
 *   "Trash" which is not its \Trash folder.
 * @param {string} [options.prefPath] - Path of a trash folder the user picked,
 *   set before the folders are discovered.
 */
async function checkTrashFolder(server, path, { strayTrash, prefPath } = {}) {
  const account = await createAccount(server, prefPath);
  const rootFolder = account.incomingServer.rootFolder;

  for (const folder of rootFolder.descendants) {
    info(
      `${folder.URI} flags=0x${folder.flags.toString(16)} ` +
        `boxFlags=0x${folder.QueryInterface(Ci.nsIMsgImapMailFolder).boxFlags.toString(16)}`
    );
  }
  const trashFolder = rootFolder.getFolderWithFlags(Ci.nsMsgFolderFlags.Trash);
  Assert.ok(trashFolder, "a folder should be flagged as trash");
  Assert.equal(
    trashFolder.QueryInterface(Ci.nsIMsgImapMailFolder).onlineName,
    path,
    "the folder reported as \\Trash should be the trash folder"
  );
  Assert.equal(
    Services.prefs.getStringPref(
      `mail.server.${account.incomingServer.key}.trash_folder_name`,
      ""
    ),
    prefPath ?? "",
    "folder discovery should not write the trash folder pref"
  );
  Assert.equal(
    !!server.daemon.getMailbox("Trash"),
    strayTrash || path == "Trash",
    "no Trash folder should have been created on the server"
  );

  // A folder which is not subscribed must not be lost on the next discovery.
  await discoverFolders(account);
  Assert.equal(
    rootFolder.getFolderWithFlags(Ci.nsMsgFolderFlags.Trash)?.URI,
    trashFolder.URI,
    "the trash folder should still be the same after another discovery"
  );

  MailServices.accounts.removeAccount(account, false);
  server.close();
}

add_task(async function testSubscribed() {
  await checkTrashFolder(
    createServer({
      trashName: "Bin",
      subscribed: true,
      prefix: "[Google Mail]",
    }),
    "[Google Mail]/Bin"
  );
});

add_task(async function testUnsubscribed() {
  await checkTrashFolder(
    createServer({
      trashName: "Bin",
      subscribed: false,
      prefix: "[Google Mail]",
    }),
    "[Google Mail]/Bin"
  );
});

add_task(async function testUnsubscribedTopLevel() {
  await checkTrashFolder(
    createServer({ trashName: "Bin", subscribed: false }),
    "Bin"
  );
});

add_task(async function testStrayTrashFolder() {
  await checkTrashFolder(
    createServer({
      trashName: "Bin",
      subscribed: true,
      prefix: "[Google Mail]",
      strayTrash: true,
    }),
    "[Google Mail]/Bin",
    { strayTrash: true }
  );
});

add_task(async function testStrayTrashFolderUnsubscribedTrash() {
  await checkTrashFolder(
    createServer({
      trashName: "Bin",
      subscribed: false,
      prefix: "[Google Mail]",
      strayTrash: true,
    }),
    "[Google Mail]/Bin",
    { strayTrash: true }
  );
});

// A trash folder the user picked wins over the one the server reports.
add_task(async function testPickedTrashFolder() {
  const server = createServer({
    trashName: "Bin",
    subscribed: true,
    prefix: "[Google Mail]",
  });
  server.daemon.createMailbox("Deleted", { subscribed: true });
  await checkTrashFolder(server, "Deleted", { prefPath: "Deleted" });
});

// A folder the user picked wins over the folder the server reports, even when
// the user picked the folder named "Trash".
add_task(async function testPickedTrashNamedTrash() {
  await checkTrashFolder(
    createServer({
      trashName: "Bin",
      subscribed: true,
      prefix: "[Google Mail]",
      strayTrash: true,
    }),
    "Trash",
    {
      strayTrash: true,
      prefPath: "Trash",
    }
  );
});

// The same, with the \Trash folder not subscribed, so it must not be listed
// and used behind the user's back.
add_task(async function testPickedTrashNamedTrashUnsubscribed() {
  await checkTrashFolder(
    createServer({
      trashName: "Bin",
      subscribed: false,
      prefix: "[Google Mail]",
      strayTrash: true,
    }),
    "Trash",
    {
      strayTrash: true,
      prefPath: "Trash",
    }
  );
});
