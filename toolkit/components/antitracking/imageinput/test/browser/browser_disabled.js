/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const PAGE = "https://example.com/document-builder.sjs?html=<body>drop here";

/**
 * The kill switch has to stop everything, not just the recording.
 *
 * It is checked ahead of the process-type gate, so flipping it also puts the
 * release assertion in the scanner out of reach. That is the whole point of it:
 * if that assertion ever fires in the wild, this pref is the remedy.
 */
add_task(async function nothing_happens_when_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.imageInputTelemetry.enabled", false]],
  });

  await BrowserTestUtils.withNewTab(PAGE, async browser => {
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
      "no event was recorded"
    );
    Assert.equal(
      Glean.imageInput.batchSize.testGetValue(),
      null,
      "not even the batch size, which would otherwise always be recorded"
    );
  });

  await SpecialPowers.popPrefEnv();
});

// And that turning it back on restores the behaviour, so the test above is
// measuring the pref rather than a broken fixture.
add_task(async function recording_resumes_when_enabled() {
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
    Assert.equal(event.extra.has_gps, "true");
  });
});
