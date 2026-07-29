/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const PAGE = `data:text/html,${encodeURIComponent(`
<!DOCTYPE HTML>
<html><body>
  <input id="single" type="file">
  <input id="multiple" type="file" multiple>
</body></html>
`)}`;

/**
 * Drives the mock file picker to hand the given files to the page, as choosing
 * them in a real picker would.
 */
async function pickFiles(aBrowser, aFiles) {
  await SpecialPowers.spawn(aBrowser, [aFiles], async files => {
    const picker = content.SpecialPowers.MockFilePicker;
    picker.init();

    const shown = new Promise(resolve => {
      picker.showCallback = () => {
        picker.setFiles(
          files.map(
            file =>
              new content.File([file.bytes], file.name, { type: file.type })
          )
        );
        picker.returnValue = picker.returnOk;
        resolve();
      };
    });

    const input = content.document.getElementById(
      files.length > 1 ? "multiple" : "single"
    );
    content.document.notifyUserGestureActivation();
    input.click();
    await shown;
  });
}

async function withPage(aTask) {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await Services.fog.testResetFOG();
    try {
      await aTask(browser);
    } finally {
      await SpecialPowers.spawn(browser, [], () =>
        content.SpecialPowers.MockFilePicker.cleanup()
      );
    }
  });
}

add_task(async function geotagged_jpeg() {
  await withPage(async browser => {
    await pickFiles(browser, [
      {
        name: "photo.jpg",
        type: "image/jpeg",
        bytes: makeJPEG(tiffBlockWithPosition()),
      },
    ]);

    const [event] = await waitForImageInputEvents(1);
    Assert.equal(event.extra.input_type, "file_picker");
    Assert.equal(event.extra.declared_type, "image/jpeg");
    Assert.equal(event.extra.detected_format, "jpeg");
    Assert.equal(event.extra.has_gps, "true");
    Assert.equal(event.extra.scan_outcome, "ok");

    // The parser decoded a real position from this photo, but only the boolean
    // reaches telemetry: the event carries exactly the declared keys and
    // nothing that could hold a latitude or longitude. This pins the reduction
    // done in ImageInputScan.
    Assert.deepEqual(
      Object.keys(event.extra).sort(),
      [
        "declared_type",
        "detected_format",
        "has_gps",
        "input_type",
        "scan_outcome",
      ],
      "the recorded event exposes no coordinate, only whether one was present"
    );
  });
});

add_task(async function jpeg_without_position() {
  await withPage(async browser => {
    await pickFiles(browser, [
      { name: "plain.jpg", type: "image/jpeg", bytes: makeJPEG(null) },
    ]);

    const [event] = await waitForImageInputEvents(1);
    Assert.equal(event.extra.has_gps, "false");
    Assert.equal(event.extra.scan_outcome, "no_exif");
  });
});

// A GPS IFD left behind with no coordinates is what a partially stripped file
// looks like, and is reported as its own state rather than a flat "no".
add_task(async function partially_stripped_jpeg() {
  await withPage(async browser => {
    await pickFiles(browser, [
      {
        name: "stripped.jpg",
        type: "image/jpeg",
        bytes: makeJPEG(tiffBlockWithStrippedPosition()),
      },
    ]);

    const [event] = await waitForImageInputEvents(1);
    Assert.equal(event.extra.has_gps, "ifd_only");
    Assert.equal(event.extra.scan_outcome, "ok");
  });
});

add_task(async function geotagged_png() {
  await withPage(async browser => {
    await pickFiles(browser, [
      {
        name: "photo.png",
        type: "image/png",
        bytes: makePNG(tiffBlockWithPosition()),
      },
    ]);

    const [event] = await waitForImageInputEvents(1);
    Assert.equal(event.extra.detected_format, "png");
    Assert.equal(event.extra.has_gps, "true");
  });
});

/**
 * The case the previous version of this probe could not see. HEIC has no entry
 * in nsMimeTypes.h and no extension mapping, so the platform often reports no
 * type for it; the file still has to be counted, and its real format still has
 * to be reported.
 */
add_task(async function heic_with_no_mime_type_is_still_counted() {
  await withPage(async browser => {
    await pickFiles(browser, [
      { name: "IMG_0001.HEIC", type: "", bytes: makeHEIC() },
    ]);

    const [event] = await waitForImageInputEvents(1);
    Assert.equal(event.extra.declared_type, "empty");
    Assert.equal(event.extra.detected_format, "heif");
    Assert.equal(
      event.extra.scan_outcome,
      "unsupported_container",
      "counted, with the reason we cannot answer the GPS question"
    );
    Assert.equal(
      event.extra.has_gps,
      "unknown",
      "not reported as a confident no"
    );
  });
});

// The type a page claims must not reach telemetry verbatim.
add_task(async function unrecognised_declared_type_is_bucketed() {
  await withPage(async browser => {
    await pickFiles(browser, [
      {
        name: "photo.jpg",
        type: "image/definitely-not-real",
        bytes: makeJPEG(tiffBlockWithPosition()),
      },
    ]);

    const [event] = await waitForImageInputEvents(1);
    Assert.equal(event.extra.declared_type, "other");
    Assert.equal(event.extra.detected_format, "jpeg");
  });
});

add_task(async function multiple_files_each_record() {
  await withPage(async browser => {
    await pickFiles(browser, [
      {
        name: "a.jpg",
        type: "image/jpeg",
        bytes: makeJPEG(tiffBlockWithPosition()),
      },
      { name: "b.jpg", type: "image/jpeg", bytes: makeJPEG(null) },
      { name: "notes.txt", type: "text/plain", bytes: new Uint8Array([1, 2]) },
    ]);

    const events = await waitForImageInputEvents(2);
    Assert.equal(events.length, 2, "the text file is not an image input");
    Assert.deepEqual(events.map(e => e.extra.has_gps).sort(), [
      "false",
      "true",
    ]);

    const batch = Glean.imageInput.batchSize.testGetValue();
    Assert.equal(batch.sum, 2, "the two images were counted, the text was not");
  });
});

/**
 * Past the scan cap the bytes are not read, but the file is still counted and
 * says why, and batch_size still reports the true total. Nothing is dropped
 * without a trace.
 */
add_task(async function batch_limit_is_visible_rather_than_silent() {
  await withPage(async browser => {
    const files = [];
    for (let i = 0; i < 12; i++) {
      files.push({
        name: `photo-${i}.jpg`,
        type: "image/jpeg",
        bytes: makeJPEG(tiffBlockWithPosition()),
      });
    }
    await pickFiles(browser, files);

    const events = await waitForImageInputEvents(12);
    Assert.equal(events.length, 12, "every file produced an event");

    const scanned = events.filter(e => e.extra.scan_outcome === "ok");
    const skipped = events.filter(
      e => e.extra.scan_outcome === "skipped_batch_limit"
    );
    Assert.equal(scanned.length, 8, "only the first few were read");
    Assert.equal(skipped.length, 4, "the rest say why they were not");

    const batch = Glean.imageInput.batchSize.testGetValue();
    Assert.equal(batch.sum, 12, "batch_size is not capped");
  });
});

add_task(async function non_image_records_nothing() {
  await withPage(async browser => {
    await assertNoEventsFrom(browser, () =>
      pickFiles(browser, [
        {
          name: "notes.txt",
          type: "text/plain",
          bytes: new Uint8Array([1, 2, 3]),
        },
      ])
    );
  });
});

add_task(async function cancelling_the_picker_records_nothing() {
  await withPage(async browser => {
    await assertNoEventsFrom(browser, async () => {
      await SpecialPowers.spawn(browser, [], async () => {
        const picker = content.SpecialPowers.MockFilePicker;
        picker.init();
        const shown = new Promise(resolve => {
          picker.showCallback = () => {
            picker.returnValue = picker.returnCancel;
            resolve();
          };
        });
        content.document.notifyUserGestureActivation();
        content.document.getElementById("single").click();
        await shown;
      });
    });
  });
});
