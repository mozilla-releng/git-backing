/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

Services.scriptloader.loadSubScript(
  new URL("head_custom_format.js", gTestPath).href,
  this
);

function writeCustomFormat(aWebCustomFormat, aText) {
  return navigator.clipboard.write([
    new ClipboardItem({
      [aWebCustomFormat]: new Blob([aText], { type: aWebCustomFormat }),
      "text/plain": new Blob([aText], { type: "text/plain" }),
    }),
  ]);
}

function readClipboardData() {
  return navigator.clipboard.read().then(async items => {
    let result = {};
    if (items.length) {
      for (let type of items[0].types) {
        let data = await items[0].getType(type);
        result[type] = await data.text();
      }
    }
    return result;
  });
}

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

  // System
  await testReadClipboardDataFromSystem(text, true);
  // Same-origin
  await testReadClipboardDataFromPage(kSameOriginUrl, text, true);
  await testReadClipboardDataFromExtensionContentScript(
    kSameOriginUrl,
    false,
    text,
    true
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
  await testReadClipboardDataFromExtensionContentScript(
    kCrossOriginUrl,
    true,
    text,
    true
  );
  // Extension background
  await testReadClipboardDataFromExtensionBackground(false, text, false);
  await testReadClipboardDataFromExtensionBackground(true, text, true);
});

add_task(async function test_data_from_extension_content_script() {
  const text = "extension-content-script-" + Math.random();
  await writeCustomFormatFromExtensionContentScript(kSameOriginUrl, text);

  // The data principal is the page's, not the extension's, so the results
  // should match test_data_from_page.
  // System
  await testReadClipboardDataFromSystem(text, true);
  // Same-origin
  await testReadClipboardDataFromPage(
    kSameOriginUrl,
    text,
    true,
    readClipboardData
  );
  await testReadClipboardDataFromExtensionContentScript(
    kSameOriginUrl,
    false,
    text,
    true
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
  await testReadClipboardDataFromExtensionContentScript(
    kCrossOriginUrl,
    true,
    text,
    true
  );
  // Extension background
  await testReadClipboardDataFromExtensionBackground(false, text, false);
  await testReadClipboardDataFromExtensionBackground(true, text, true);
});

add_task(async function test_data_from_extension_background() {
  const text = "extension-background-" + Math.random();
  await writeCustomFormatFromExtensionBackground(text);

  // The data principal is the writing extension's moz-extension: principal.
  // Each read helper below installs a different extension, so none of them is
  // same-origin with the data.
  // System
  await testReadClipboardDataFromSystem(text, true);
  // Pages
  for (const url of [kSameOriginUrl, kCrossOriginUrl]) {
    await testReadClipboardDataFromPage(url, text, false);
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
  // Extension background
  await testReadClipboardDataFromExtensionBackground(false, text, false);
  await testReadClipboardDataFromExtensionBackground(true, text, true);
});

add_task(async function test_data_from_system() {
  const text = "system-" + Math.random();
  info(`Write ${text} to clipboard from system`);
  await writeCustomFormat(kWebCustomFormat, text);

  // System
  await testReadClipboardDataFromSystem(text, true);
  // Pages
  for (const url of [kSameOriginUrl, kCrossOriginUrl]) {
    await testReadClipboardDataFromPage(url, text, false);
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
  // Extension background
  await testReadClipboardDataFromExtensionBackground(false, text, false);
  await testReadClipboardDataFromExtensionBackground(true, text, true);
});

add_task(async function test_cross_origin_pref_enabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["clipboard.readCrossOriginCustomFormats.enabled", true]],
  });

  const text = "cross-origin-pre-enabled-" + Math.random();
  await writeCustomFormatFromPage(kSameOriginUrl, text);

  // System
  await testReadClipboardDataFromSystem(text, true);
  // Pages
  for (const url of [kSameOriginUrl, kCrossOriginUrl]) {
    await testReadClipboardDataFromPage(url, text, true);
    await testReadClipboardDataFromExtensionContentScript(
      url,
      false,
      text,
      true
    );
    await testReadClipboardDataFromExtensionContentScript(
      url,
      true,
      text,
      true
    );
  }
  // Extension background
  await testReadClipboardDataFromExtensionBackground(false, text, true);
  await testReadClipboardDataFromExtensionBackground(true, text, true);

  await SpecialPowers.popPrefEnv();
});
