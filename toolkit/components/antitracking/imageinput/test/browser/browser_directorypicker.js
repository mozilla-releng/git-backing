/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PromptTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromptTestUtils.sys.mjs"
);

const PAGE = `data:text/html,${encodeURIComponent(`
<!DOCTYPE HTML>
<html><body>
  <input id="folder" type="file" webkitdirectory>
</body></html>
`)}`;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      // The parent refuses to enumerate a directory a content process has not
      // been granted access to, and a mock file picker running in content never
      // makes that grant. This is the switch tests use to get past it.
      ["dom.filesystem.pathcheck.disabled", true],
      ["dom.webkitBlink.dirPicker.enabled", true],
      // The upload confirmation's Accept button is disabled for a moment as an
      // anti-clickjacking measure; there is nothing to protect against here.
      ["security.dialog_enable_delay", 0],
    ],
  });
});

/**
 * A folder holding two geotagged photos, one photo without a position, and a
 * file that is not an image at all.
 *
 * Built in the parent process: the content process cannot write files, and the
 * directory has to exist on disk before the picker can hand it over.
 */
async function createPhotoFolder() {
  const root = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "imageinput-folder"
  );

  await IOUtils.write(
    PathUtils.join(root, "a.jpg"),
    makeJPEG(tiffBlockWithPosition())
  );
  await IOUtils.write(
    PathUtils.join(root, "b.jpg"),
    makeJPEG(tiffBlockWithPosition())
  );
  await IOUtils.write(PathUtils.join(root, "c.jpg"), makeJPEG(null));
  await IOUtils.write(
    PathUtils.join(root, "notes.txt"),
    new TextEncoder().encode("not an image")
  );

  registerCleanupFunction(async () => {
    await IOUtils.remove(root, { recursive: true, ignoreAbsent: true });
  });

  return root;
}

/**
 * Uploading a folder is the gap the previous version of this probe had.
 *
 * The picker's completion callback never sees the files: for a folder it is
 * handed a Directory and returns, and the files only appear later, once the
 * enumeration behind it finishes. Counting them therefore has to happen in that
 * later callback, and this test is what proves it does -- and that it does not
 * also count them a second time from the picker path.
 */
add_task(async function folder_upload_records_each_image_once() {
  const folder = await createPhotoFolder();

  await BrowserTestUtils.withNewTab(PAGE, async browser => {
    await Services.fog.testResetFOG();

    const changed = SpecialPowers.spawn(browser, [], async () => {
      const input = content.document.getElementById("folder");
      return ContentTaskUtils.waitForEvent(input, "change").then(
        e => e.target.files.length
      );
    });

    const promptShown = PromptTestUtils.waitForPrompt(browser, {
      modalType: Services.prompt.MODAL_TYPE_TAB,
      promptType: "confirmEx",
    });

    try {
      await SpecialPowers.spawn(browser, [folder], async path => {
        const picker = content.SpecialPowers.MockFilePicker;
        picker.init();
        picker.useDirectory(path);
        picker.returnValue = picker.returnOk;

        content.SpecialPowers.wrap(
          content.document
        ).notifyUserGestureActivation();
        content.document.getElementById("folder").click();
      });

      // Choosing a folder asks the user to confirm before the page is given
      // access to it, and nothing is handed over until they do.
      const prompt = await promptShown;
      await PromptTestUtils.handlePrompt(prompt, { buttonNumClick: 0 });

      const fileCount = await changed;
      Assert.equal(fileCount, 4, "the page was given every file in the folder");

      const events = await waitForImageInputEvents(3);
      Assert.equal(
        events.length,
        3,
        "three images, counted once each and not twice"
      );

      for (const event of events) {
        Assert.equal(
          event.extra.input_type,
          "directory_picker",
          "the folder picker is distinguishable from the file picker"
        );
        Assert.equal(event.extra.detected_format, "jpeg");
      }

      Assert.deepEqual(
        events.map(e => e.extra.has_gps).sort(),
        ["false", "true", "true"],
        "the text file was ignored and the plain JPEG reported no position"
      );

      const batch = Glean.imageInput.batchSize.testGetValue();
      Assert.equal(batch.sum, 3, "only the images were counted");
    } finally {
      await SpecialPowers.spawn(browser, [], () =>
        content.SpecialPowers.MockFilePicker.cleanup()
      );
    }
  });
});
