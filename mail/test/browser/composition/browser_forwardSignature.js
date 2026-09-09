/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Tests that the signature is inserted when forwarding inline.
 */

"use strict";

var { close_compose_window, get_compose_body, open_compose_with_forward } =
  ChromeUtils.importESModule(
    "resource://testing-common/mail/ComposeHelpers.sys.mjs"
  );
var { be_in_folder, create_folder, select_click_row } =
  ChromeUtils.importESModule(
    "resource://testing-common/mail/FolderDisplayHelpers.sys.mjs"
  );
var { add_message_to_folder, create_message } = ChromeUtils.importESModule(
  "resource://testing-common/mail/MessageInjectionHelpers.sys.mjs"
);

var sig = "roses are red";
var folder;

add_setup(async function () {
  folder = await create_folder("ForwardSigTest");
  registerCleanupFunction(() => folder.deleteSelf(null));

  await add_message_to_folder(
    [folder],
    create_message({
      subject: "message to forward",
      body: {
        body: "Hello, this is the original message body.\n",
        contentType: "text/plain",
        charset: "UTF-8",
      },
    })
  );
});

async function check_forward_signature(sigBottom) {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["mail.identity.default.compose_html", false],
      ["mail.identity.default.reply_on_top", 1],
      ["mail.identity.default.sig_bottom", sigBottom],
      ["mail.identity.default.sig_on_fwd", true],
      ["mail.identity.default.htmlSigText", sig],
      ["mail.identity.default.htmlSigFormat", false],
      ["mail.forward_message_mode", 2],
    ],
  });

  await be_in_folder(folder);
  await select_click_row(0);

  const cw = await open_compose_with_forward();
  const body = get_compose_body(cw);
  info(`body innerHTML for sig_bottom=${sigBottom}: ${body.innerHTML}`);
  Assert.ok(
    body.textContent.includes("original message body"),
    `forwarded content should be present (sig_bottom=${sigBottom})`
  );
  Assert.ok(
    body.textContent.includes(sig),
    `signature should be present (sig_bottom=${sigBottom})`
  );
  await close_compose_window(cw);
  await SpecialPowers.popPrefEnv();
}

add_task(async function test_forward_plaintext_signature_below_quote() {
  await check_forward_signature(true);
});

add_task(async function test_forward_plaintext_signature_above_quote() {
  await check_forward_signature(false);
});
