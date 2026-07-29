/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// An editable body, so that the paste key runs the editor's own paste command
// and a trusted paste event follows. A paste event also fires on a page with
// nothing editable in it, and on middle click with no editor at all; the editor
// is simply the reliable way to produce one here.
const PAGE =
  "https://example.com/document-builder.sjs?html=" +
  encodeURIComponent("<body contenteditable>paste here</body>");

/**
 * Puts a real file on the system clipboard.
 *
 * A constructed ClipboardEvent cannot be used here: ClipboardEventInit has no
 * clipboardData member, only the legacy data and dataType strings, so an event
 * built in script cannot carry a file. The key press below drives the editor's
 * own paste command, which is what produces a trusted event carrying one.
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

/**
 * Copies an image out of the page, which puts it on the clipboard as pixels
 * rather than as a file: the shape a screenshot arrives in.
 */
async function putImageOnClipboard(aBrowser) {
  await SpecialPowers.spawn(aBrowser, [], async () => {
    const image = content.document.createElement("img");
    const loaded = new Promise(resolve => {
      image.addEventListener("load", resolve, { once: true });
    });
    image.src =
      "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAA" +
      "DUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==";
    content.document.body.appendChild(image);
    await loaded;

    const documentViewer = content.docShell.docViewer.QueryInterface(
      Ci.nsIDocumentViewerEdit
    );
    documentViewer.setCommandNode(image);
    documentViewer.copyImage(documentViewer.COPY_IMAGE_ALL);
    image.remove();
  });
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

// A clipboard holding no file must not produce an event. Most pastes are text,
// so this is also the common case.
add_task(async function pasting_text_records_nothing() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    const event = await assertNoEventsFrom(
      browser,
      async () => {
        putTextOnClipboard("just some text");
        await pasteIntoPage(browser);
      },
      // Control with a paste rather than the default drop, so that the paste
      // path is the only one this test can fail on.
      async aBrowser => {
        await putFileOnClipboard(
          makeJPEG(tiffBlockWithPosition()),
          "control.jpg"
        );
        await pasteIntoPage(aBrowser);
      }
    );

    Assert.equal(event.extra.input_type, "paste");
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

/**
 * A screenshot on the clipboard reaches the page as a file too, but one the
 * platform holds as pixels. Turning it into a File means decoding and
 * re-encoding it synchronously, so the probe counts it and leaves it unread.
 */
add_task(async function pasted_bitmap_is_counted_but_not_read() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await Services.fog.testResetFOG();
    await putImageOnClipboard(browser);

    await pasteIntoPage(browser);

    const [event] = await waitForImageInputEvents(1);
    Assert.equal(event.extra.input_type, "paste");
    Assert.equal(event.extra.scan_outcome, "skipped_bitmap");
    Assert.equal(event.extra.detected_format, "not_scanned");
    Assert.equal(event.extra.has_gps, "unknown");

    const batch = Glean.imageInput.batchSize.testGetValue();
    Assert.equal(batch.sum, 1, "counted towards the selection size");
  });
});
