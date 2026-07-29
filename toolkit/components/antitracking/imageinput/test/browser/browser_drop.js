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

    await SpecialPowers.spawn(
      browser,
      [makeJPEG(tiffBlockWithPosition())],
      async bytes => {
        const transfer = new content.DataTransfer();
        transfer.items.add(
          new content.File([bytes], "photo.jpg", { type: "image/jpeg" })
        );

        const event = content.document.createEvent("DragEvent");
        event.initDragEvent(
          "drop",
          true,
          true,
          content.window,
          0,
          0,
          0,
          0,
          0,
          false,
          false,
          false,
          false,
          0,
          content.document.body,
          transfer
        );

        const seen = new Promise(resolve => {
          content.document.addEventListener(
            "drop",
            e => {
              // Three separate reads of the same event.
              void e.dataTransfer;
              void e.dataTransfer;
              void e.dataTransfer;
              resolve();
            },
            { once: true }
          );
        });

        content.document.dispatchEvent(event);
        await seen;
      }
    );

    const events = await waitForImageInputEvents(1);
    Assert.equal(events.length, 1, "one drop, one event");
  });
});

/**
 * The file input's own listener asks for the transfer on dragover, to decide
 * whether it can accept the drag. Counting that would mean scanning files every
 * time the pointer crosses a file input.
 */
add_task(async function dragover_records_nothing() {
  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await assertNoEventsFrom(browser, async () => {
      await SpecialPowers.spawn(
        browser,
        [makeJPEG(tiffBlockWithPosition())],
        async bytes => {
          const transfer = new content.DataTransfer();
          transfer.items.add(
            new content.File([bytes], "photo.jpg", { type: "image/jpeg" })
          );

          const event = content.document.createEvent("DragEvent");
          event.initDragEvent(
            "dragover",
            true,
            true,
            content.window,
            0,
            0,
            0,
            0,
            0,
            false,
            false,
            false,
            false,
            0,
            content.document.body,
            transfer
          );

          const seen = new Promise(resolve => {
            content.document.addEventListener(
              "dragover",
              e => {
                void e.dataTransfer;
                resolve();
              },
              { once: true }
            );
          });

          content.document.dispatchEvent(event);
          await seen;
        }
      );
    });
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
