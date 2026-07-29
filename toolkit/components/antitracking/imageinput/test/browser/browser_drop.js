/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const PAGE = "https://example.com/document-builder.sjs?html=<body>drop here";

add_task(async function dropped_image_is_recorded() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await Services.fog.testResetFOG();

    await dropFilesOnPage(browser, [
      {
        name: "photo.jpg",
        type: "image/jpeg",
        bytes: makeJPEG(tiffBlockWithPosition()),
      },
    ]);

    const [event] = await waitForImageInputEvents(1);
    Assert.equal(event.extra.input_type, "drop");
    Assert.equal(event.extra.detected_format, "jpeg");
    Assert.equal(event.extra.has_gps, "true");
  });
});

/**
 * The page may look at dataTransfer as many times as it likes; the files still
 * only arrived once.
 */
add_task(async function repeated_data_transfer_access_records_once() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await Services.fog.testResetFOG();

    await dropFilesOnPage(
      browser,
      [
        {
          name: "photo.jpg",
          type: "image/jpeg",
          bytes: makeJPEG(tiffBlockWithPosition()),
        },
      ],
      3
    );

    const events = await waitForImageInputEvents(1);
    Assert.equal(events.length, 1, "one drop, one event");
  });
});

/**
 * The file input's own listener asks for the transfer on dragover, to decide
 * whether it can accept the drag. Counting that would mean scanning files every
 * time the pointer crosses a file input. The event is trusted like the drop is,
 * so its message is the only thing keeping it out.
 */
add_task(async function dragover_records_nothing() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await assertNoEventsFrom(browser, () =>
      dragFilesOverPage(browser, [
        {
          name: "photo.jpg",
          type: "image/jpeg",
          bytes: makeJPEG(tiffBlockWithPosition()),
        },
      ])
    );
  });
});

/**
 * A page that builds a drop and dispatches it at itself is handed nothing: it
 * already had the File it put in the transfer. Only the trust bit separates
 * this from dropped_image_is_recorded above, so it is what pins the gate.
 */
add_task(async function a_drop_from_page_script_records_nothing() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await assertNoEventsFrom(browser, () =>
      dropFilesFromPageScript(browser, [
        {
          name: "photo.jpg",
          type: "image/jpeg",
          bytes: makeJPEG(tiffBlockWithPosition()),
        },
      ])
    );
  });
});

add_task(async function dropping_a_non_image_records_nothing() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await assertNoEventsFrom(browser, () =>
      dropFilesOnPage(browser, [
        {
          name: "notes.txt",
          type: "text/plain",
          bytes: new Uint8Array([1, 2, 3]),
        },
      ])
    );
  });
});

/**
 * The browser's own pages are not sites being handed a photo, and the file's
 * bytes must not be read for them. about:preferences additionally runs with a
 * system principal, so this also covers the guard that keeps the release
 * assertion in the scanner out of reach.
 */
add_task(async function browser_pages_record_nothing() {
  for (const url of ["about:preferences", "about:newtab"]) {
    await BrowserTestUtils.withNewTab(url, async browser => {
      await Services.fog.testResetFOG();

      await dropFilesOnPage(browser, [
        {
          name: "photo.jpg",
          type: "image/jpeg",
          bytes: makeJPEG(tiffBlockWithPosition()),
        },
      ]);

      await Services.fog.testFlushAllChildren();
      Assert.equal(
        Glean.imageInput.offered.testGetValue(),
        null,
        `${url} recorded nothing`
      );
    });
  }
});
