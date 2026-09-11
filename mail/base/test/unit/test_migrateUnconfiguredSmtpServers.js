/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test that SMTP servers left behind unconfigured by an interrupted account
 * setup are removed. See bug 2069949.
 */

var { MailMigrator } = ChromeUtils.importESModule(
  "resource:///modules/MailMigrator.sys.mjs"
);
var { MailServices } = ChromeUtils.importESModule(
  "resource:///modules/MailServices.sys.mjs"
);
var { localAccountUtils } = ChromeUtils.importESModule(
  "resource://testing-common/mailnews/LocalAccountUtils.sys.mjs"
);

add_task(async function test_migrateUnconfiguredSmtpServers() {
  const configured = localAccountUtils.create_outgoing_server(
    "smtp",
    "username",
    "password",
    { hostname: "smtp.invalid", port: 1234 }
  );
  // A server as createServer() leaves it, before a hostname has been set.
  const unconfigured = MailServices.outgoingServer.createServer("smtp");
  unconfigured.description = "Leftover";
  unconfigured.username = "username";
  MailServices.outgoingServer.defaultServer = unconfigured;

  Services.prefs.setIntPref("mail.ui-rdf.version", 64);
  MailMigrator._migrateUI();

  Assert.deepEqual(
    MailServices.outgoingServer.servers.map(server => server.key),
    [configured.key],
    "the unconfigured server should have been removed"
  );
  Assert.equal(
    Services.prefs.getStringPref(
      `mail.smtpserver.${unconfigured.key}.description`,
      ""
    ),
    "",
    "the removed server's preferences should have been cleared"
  );
  Assert.equal(
    MailServices.outgoingServer.defaultServer.key,
    configured.key,
    "the remaining server should have become the default"
  );
});
