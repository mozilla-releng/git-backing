/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const kCustomFormat = "text/x-custom-format-origin-test";
const kWebCustomFormat = "web " + kCustomFormat;
const kSameOriginUrl = "https://example.com/";
const kCrossOriginUrl = "https://example.org/";

async function installAndStartExtension(
  aUrl,
  aHasClipboardReadPermission,
  aWriteFunc,
  aReadFunc
) {
  const extensionScript = aMsgPrefix => {
    return `browser.test.onMessage.addListener(async (aMsg, aText) => {
      if (aMsg === "${aMsgPrefix}ClipboardRead") {
        let result = await (${aReadFunc.toString()})();
        browser.test.sendMessage("${aMsgPrefix}ClipboardReadResult", result);
      } else if (aMsg === "${aMsgPrefix}ClipboardWrite") {
        await (${aWriteFunc.toString()})(${JSON.stringify(kWebCustomFormat)}, aText);
        browser.test.sendMessage("${aMsgPrefix}ClipboardWriteDone");
      }
    });`;
  };

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: aHasClipboardReadPermission ? ["clipboardRead"] : [],
      content_scripts: [
        {
          js: ["content_script.js"],
          matches: [`${aUrl}*`],
        },
      ],
    },
    files: {
      "content_script.js": extensionScript("content"),
    },
    background: extensionScript("background"),
  });

  await extension.startup();

  return extension;
}

/**
 * Write utility functions
 */

function writeCustomFormatFromPage(
  aUrl,
  aText,
  aWriteFunc = writeCustomFormat
) {
  info(`Write ${aText} to clipboard from ${aUrl}`);
  return BrowserTestUtils.withNewTab(aUrl, async browser => {
    await SpecialPowers.spawn(
      browser,
      [aWriteFunc.toString(), kWebCustomFormat, aText],
      async (aWriteFunc, aFormat, aText) => {
        content.document.notifyUserGestureActivation();
        // Run the clipboard calls in the page realm so that they use the page
        // principal rather than the SpecialPowers sandbox one.
        await content.wrappedJSObject.eval(
          `(${aWriteFunc})(${JSON.stringify(aFormat)}, ${JSON.stringify(aText)})`
        );
      }
    );
  });
}

async function writeCustomFormatFromExtensionContentScript(
  aUrl,
  aText,
  aWriteFunc = writeCustomFormat,
  aReadFunc = readClipboardData
) {
  info(`Write ${aText} to clipboard from extension content script for ${aUrl}`);
  const extension = await installAndStartExtension(
    aUrl,
    false,
    aWriteFunc,
    aReadFunc
  );
  await BrowserTestUtils.withNewTab(aUrl, async browser => {
    await SpecialPowers.spawn(browser, [], () => {
      content.document.notifyUserGestureActivation();
    });
    extension.sendMessage("contentClipboardWrite", aText);
    await extension.awaitMessage("contentClipboardWriteDone");
  });
  await extension.unload();
}

async function writeCustomFormatFromExtensionBackground(
  aText,
  aWriteFunc = writeCustomFormat,
  aReadFunc = readClipboardData
) {
  info(`Write ${aText} to clipboard from extension background`);
  const extension = await installAndStartExtension(
    "https://foo.bar/",
    false,
    aWriteFunc,
    aReadFunc
  );
  extension.sendMessage("backgroundClipboardWrite", aText);
  await extension.awaitMessage("backgroundClipboardWriteDone");
  await extension.unload();
}

/**
 * Read utility functions
 */

function readClipboardDataFromPage(aUrl, aReadFunc) {
  return BrowserTestUtils.withNewTab(aUrl, async browser => {
    return SpecialPowers.spawn(
      browser,
      [aReadFunc.toString()],
      async aReadFunc => {
        content.document.notifyUserGestureActivation();
        const result = await content.wrappedJSObject.eval(
          `(${aReadFunc})().then(JSON.stringify)`
        );
        return JSON.parse(result);
      }
    );
  });
}

async function readClipboardTypesFromExtensionContentScript(
  aUrl,
  aHasClipboardReadPermission,
  aWriteFunc,
  aReadFunc
) {
  const extension = await installAndStartExtension(
    aUrl,
    aHasClipboardReadPermission,
    aWriteFunc,
    aReadFunc
  );
  let result = await BrowserTestUtils.withNewTab(aUrl, async browser => {
    await SpecialPowers.spawn(browser, [], () => {
      content.document.notifyUserGestureActivation();
    });
    extension.sendMessage("contentClipboardRead");
    return await extension.awaitMessage("contentClipboardReadResult");
  });

  await extension.unload();
  return result;
}

async function readClipboardDataFromExtensionBackground(
  aHasClipboardReadPermission,
  aWriteFunc,
  aReadFunc
) {
  const extension = await installAndStartExtension(
    "https://foo.bar/",
    aHasClipboardReadPermission,
    aWriteFunc,
    aReadFunc
  );
  extension.sendMessage("backgroundClipboardRead");
  const result = await extension.awaitMessage("backgroundClipboardReadResult");
  await extension.unload();
  return result;
}

/**
 * Test utility functions
 */

async function testClipboardData(
  aClipboardData,
  aExpectedText,
  aShouldGetCustomData
) {
  SimpleTest.isDeeply(
    Object.keys(aClipboardData),
    aShouldGetCustomData ? [kWebCustomFormat, "text/plain"] : ["text/plain"],
    "Check data types"
  );
  is(
    aClipboardData[kWebCustomFormat],
    aShouldGetCustomData ? aExpectedText : undefined,
    "Check web custom format data"
  );
  is(aClipboardData[kCustomFormat], undefined, "Check custom format data");
  is(aClipboardData["text/plain"], aExpectedText, "Check text/plain data");
}

async function testReadClipboardDataFromSystem(
  aExpectedText,
  aShouldGetCustomData,
  aReadFunc = readClipboardData
) {
  info(
    `Test reading clipboard data from system should ${aShouldGetCustomData ? "see" : "hide"} web custom format`
  );

  const result = await aReadFunc();
  testClipboardData(result, aExpectedText, aShouldGetCustomData);
}

async function testReadClipboardDataFromPage(
  aUrl,
  aExpectedText,
  aShouldGetCustomData,
  aReadFunc = readClipboardData
) {
  info(
    `Test reading clipboard data from ${aUrl} should ${aShouldGetCustomData ? "see" : "hide"} web custom format`
  );

  const result = await readClipboardDataFromPage(aUrl, aReadFunc);
  testClipboardData(result, aExpectedText, aShouldGetCustomData);
}

async function testReadClipboardDataFromExtensionContentScript(
  aUrl,
  aHasClipboardReadPermission,
  aExpectedText,
  aShouldGetCustomData,
  aWriteFunc = writeCustomFormat,
  aReadFunc = readClipboardData
) {
  info(
    `Test reading clipboard data from extension content script ${aHasClipboardReadPermission ? "with" : "without"} clipboardRead permission for ${aUrl} should ${aShouldGetCustomData ? "see" : "hide"} web custom format`
  );

  const result = await readClipboardTypesFromExtensionContentScript(
    aUrl,
    aHasClipboardReadPermission,
    aWriteFunc,
    aReadFunc
  );
  testClipboardData(result, aExpectedText, aShouldGetCustomData);
}

async function testReadClipboardDataFromExtensionBackground(
  aHasClipboardReadPermission,
  aExpectedText,
  aShouldGetCustomData,
  aWriteFunc = writeCustomFormat,
  aReadFunc = readClipboardData
) {
  info(
    `Test reading clipboard data from extension background ${aHasClipboardReadPermission ? "with" : "without"} clipboardRead permission should ${aShouldGetCustomData ? "see" : "hide"} web custom format`
  );

  const result = await readClipboardDataFromExtensionBackground(
    aHasClipboardReadPermission,
    aWriteFunc,
    aReadFunc
  );
  testClipboardData(result, aExpectedText, aShouldGetCustomData);
}
