/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

Services.scriptloader.loadSubScript(
  new URL("head_custom_format.js", gTestPath).href,
  this
);

function writeCustomFormat(aWebCustomFormat, aText) {
  let promise = new Promise(resolve => {
    document.addEventListener(
      "copy",
      e => {
        let dataTransfer = e.clipboardData;
        dataTransfer.setData("text/plain", aText);
        dataTransfer.setData(aWebCustomFormat, aText);
        e.preventDefault();
        resolve();
      },
      { once: true }
    );
  });
  document.execCommand("copy");
  return promise;
}

// Pastes with execCommand("paste") into a temporary textarea and returns the
// paste event's clipboardData as an object mapping each type to its data, or
// null if the paste wasn't allowed.
function readClipboardData() {
  let promise = new Promise(resolve => {
    document.addEventListener(
      "paste",
      e => {
        let dataTransfer = e.clipboardData;
        let result = {};
        for (const type of dataTransfer.types) {
          result[type] = dataTransfer.getData(type);
        }
        e.preventDefault();
        resolve(result);
      },
      { once: true }
    );
  });
  document.execCommand("paste");
  return promise;
}

// Unlike navigator.clipboard.read(), execCommand("paste") reads with the
// document principal of the window it runs in. So an extension content script
// reads as the page, and its clipboardRead permission doesn't let it see
// cross-origin web custom formats.

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["dom.events.testing.asyncClipboard", true],
      ["dom.clipboard.customFormatSupport.enabled", true],
      ["clipboard.readCustomFormatsFromClipboard.enabled", true],
      ["clipboard.readCrossOriginCustomFormats.enabled", false],
    ],
  });
});

add_task(async function test_data_from_page() {
  const text = "page-" + Math.random();
  await writeCustomFormatFromPage(kSameOriginUrl, text);

  // Same-origin
  // XXX: same-origin page should be able to get custom data by execCommand.
  await testReadClipboardDataFromPage(kSameOriginUrl, text, false);
  // XXX: extension content script without clipboardRead permission for
  // same-origin page should be able to get custom data by execCommand.
  await testReadClipboardDataFromExtensionContentScript(
    kSameOriginUrl,
    false,
    text,
    false
  );
  await testReadClipboardDataFromExtensionContentScript(
    kSameOriginUrl,
    true,
    text,
    true
  );
  // Cross-origin
  await testReadClipboardDataFromPage(kCrossOriginUrl, text, false);
  await testReadClipboardDataFromExtensionContentScript(
    kCrossOriginUrl,
    false,
    text,
    false
  );
  // XXX: extension content script with clipboardRead permission should be
  // able to get custom data by execCommand.
  await testReadClipboardDataFromExtensionContentScript(
    kCrossOriginUrl,
    true,
    text,
    false
  );
});

add_task(async function test_data_from_extension_content_script() {
  const text = "extension-content-script-" + Math.random();
  await writeCustomFormatFromExtensionContentScript(kSameOriginUrl, text);

  // Same-origin
  // XXX: same-origin page should be able to get custom data by execCommand.
  await testReadClipboardDataFromPage(kSameOriginUrl, text, false);
  // XXX: extension content script without clipboardRead permission for
  // same-origin page should be able to get custom data by execCommand.
  await testReadClipboardDataFromExtensionContentScript(
    kSameOriginUrl,
    false,
    text,
    false
  );
  await testReadClipboardDataFromExtensionContentScript(
    kSameOriginUrl,
    true,
    text,
    true
  );
  // Cross-origin
  await testReadClipboardDataFromPage(kCrossOriginUrl, text, false);
  await testReadClipboardDataFromExtensionContentScript(
    kCrossOriginUrl,
    false,
    text,
    false
  );
  // XXX: extension content script with clipboardRead permission should be
  // able to get custom data by execCommand.
  await testReadClipboardDataFromExtensionContentScript(
    kCrossOriginUrl,
    true,
    text,
    false
  );
});

add_task(async function test_cross_origin_pref_enabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["clipboard.readCrossOriginCustomFormats.enabled", true]],
  });

  const text = "cross-origin-pre-enabled-" + Math.random();
  await writeCustomFormatFromPage(kSameOriginUrl, text);

  // Pages
  for (const url of [kSameOriginUrl, kCrossOriginUrl]) {
    // XXX: execommand should be able to get custom data when pref is on.
    await testReadClipboardDataFromPage(url, text, false);
    // XXX: execommand should be able to get custom data when pref is on.
    await testReadClipboardDataFromExtensionContentScript(
      url,
      false,
      text,
      false
    );
    await testReadClipboardDataFromExtensionContentScript(
      url,
      true,
      text,
      true
    );
  }

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_data_without_principal() {
  const text = "no-principal-" + Math.random();
  info(`Write ${text} to clipboard without principal`);

  const trans = Cc["@mozilla.org/widget/transferable;1"].createInstance(
    Ci.nsITransferable
  );
  trans.init(null);
  [kWebCustomFormat, kCustomFormat, "text/plain"].forEach(type => {
    const data = Cc["@mozilla.org/supports-cstring;1"].createInstance(
      Ci.nsISupportsCString
    );
    data.data = text;
    trans.addDataFlavor(type);
    trans.setTransferData(type, data);
  });
  Services.clipboard.setData(trans, null, Ci.nsIClipboard.kGlobalClipboard);

  // Pages
  for (const url of [kSameOriginUrl, kCrossOriginUrl]) {
    await testReadClipboardDataFromPage(url, text, false);
    await testReadClipboardDataFromExtensionContentScript(
      url,
      false,
      text,
      false
    );
    // XXX: extension content script with clipboardRead permission should be
    // able to get custom data by execCommand.
    await testReadClipboardDataFromExtensionContentScript(
      url,
      true,
      text,
      false
    );
  }
});
