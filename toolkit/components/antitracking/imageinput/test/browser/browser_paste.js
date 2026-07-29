/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// The paste command is only enabled for an editable target, so pressing the key
// on an ordinary page dispatches nothing at all.
const PAGE =
  "https://example.com/document-builder.sjs?html=" +
  encodeURIComponent("<body contenteditable>paste here</body>");

/**
 * Puts a real file on the system clipboard.
 *
 * A constructed ClipboardEvent cannot be used here: ClipboardEventInit has no
 * clipboardData member, only the legacy data and dataType strings, so an event
 * built in script cannot carry a file. The parent converts the clipboard's
 * nsIFile into a blob on its way to the content process, which is why no
 * special pref is needed for it to arrive.
 */
async function putFileOnClipboard(aBytes, aName) {
  const path = PathUtils.join(PathUtils.tempDir, aName);
  await IOUtils.write(path, aBytes);
  registerCleanupFunction(() => IOUtils.remove(path, { ignoreAbsent: true }));

  const file = Cc["@mozilla.org/file/local;1"].createInstance(Ci.nsIFile);
  file.initWithPath(path);

  const transferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
    Ci.nsITransferable
  );
  transferable.init(null);
  transferable.addDataFlavor("application/x-moz-file");
  transferable.setTransferData("application/x-moz-file", file);

  Services.clipboard.setData(
    transferable,
    null,
    Ci.nsIClipboard.kGlobalClipboard
  );
}

function putTextOnClipboard(aText) {
  const string = Cc["@mozilla.org/supports-string;1"].createInstance(
    Ci.nsISupportsString
  );
  string.data = aText;

  const transferable = Cc["@mozilla.org/widget/transferable;1"].createInstance(
    Ci.nsITransferable
  );
  transferable.init(null);
  transferable.addDataFlavor("text/plain");
  transferable.setTransferData("text/plain", string);

  Services.clipboard.setData(
    transferable,
    null,
    Ci.nsIClipboard.kGlobalClipboard
  );
}

/**
 * Presses the paste key with a listener installed that reads clipboardData,
 * which is what hands the files to the page.
 *
 * Deliberately does not wait for the paste event: if it never arrives the
 * telemetry poll that follows reports that clearly, rather than hanging until
 * the harness gives up.
 */
async function pasteIntoPage(aBrowser, aReads = 1) {
  await SpecialPowers.spawn(aBrowser, [aReads], async reads => {
    content.document.body.focus();
    content.document.addEventListener(
      "paste",
      e => {
        for (let i = 0; i < reads; i++) {
          void e.clipboardData;
        }
      },
      { once: true }
    );
  });

  await SimpleTest.promiseFocus(aBrowser);
  await BrowserTestUtils.synthesizeKey("v", { accelKey: true }, aBrowser);
}

add_task(async function pasted_image_is_recorded() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await Services.fog.testResetFOG();
    await putFileOnClipboard(makeJPEG(tiffBlockWithPosition()), "paste.jpg");

    await pasteIntoPage(browser);

    const [event] = await waitForImageInputEvents(1);
    Assert.equal(event.extra.input_type, "paste");
    Assert.equal(event.extra.detected_format, "jpeg");
    Assert.equal(event.extra.has_gps, "true");
  });
});

/**
 * A clipboard holding no file must not produce an event. This is also the case
 * that matters for cost, since most pastes are text and reading clipboardData
 * is what forces the transfer to materialise.
 */
add_task(async function pasting_text_records_nothing() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await Services.fog.testResetFOG();

    putTextOnClipboard("just some text");
    await pasteIntoPage(browser);

    // Then something that definitely does record, so the absence above is
    // proven rather than merely waited for.
    await recordKnownGoodEvent(browser);

    const events = await waitForImageInputEvents(1);
    Assert.equal(events.length, 1, "only the drop recorded");
    Assert.equal(events[0].extra.input_type, "drop");
  });
});

// The page may read clipboardData as often as it likes; the files still only
// arrived once.
add_task(async function repeated_clipboard_data_access_records_once() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await Services.fog.testResetFOG();
    await putFileOnClipboard(makeJPEG(tiffBlockWithPosition()), "paste2.jpg");

    await pasteIntoPage(browser, 3);

    const events = await waitForImageInputEvents(1);
    Assert.equal(events.length, 1, "one paste, one event");
  });
});
