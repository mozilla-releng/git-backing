/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
const { MockFilePicker } = ChromeUtils.importESModule(
  "resource://testing-common/MockFilePicker.sys.mjs"
);
const { ZipArchive } = ChromeUtils.importESModule(
  "resource:///modules/ZipArchive.sys.mjs"
);

const realCreate = ZipArchive.create;

add_setup(() => {
  MockFilePicker.init(window.browsingContext);
  registerCleanupFunction(() => {
    MockFilePicker.cleanup();
    ZipArchive.create = realCreate;
  });
});

/**
 * Replace ZipArchive.create with one that writes a little, reports some
 * progress, then waits until it is aborted, like an export of a large
 * profile.
 *
 * @returns {{aborted: Promise}} `aborted` resolves when the archive is
 *   aborted.
 */
function stubSlowArchive() {
  const { promise: aborted, resolve: onAborted } = Promise.withResolvers();
  ZipArchive.create = async (archivePath, entries, onProgress, { signal }) => {
    await IOUtils.writeUTF8(archivePath, "partial");
    onProgress(1, 2);
    if (!signal.aborted) {
      await new Promise(resolve =>
        signal.addEventListener("abort", resolve, { once: true })
      );
    }
    onAborted();
    throw signal.reason;
  };
  return { aborted };
}

/**
 * Choose where to save the backup, open the export tab and start exporting.
 *
 * @returns {Promise<{tab: object, zipFile: string}>}
 */
async function startExport() {
  const profileDir = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "profile-tmp"
  );
  registerCleanupFunction(() =>
    IOUtils.remove(profileDir, { recursive: true })
  );
  const zipFile = PathUtils.join(profileDir, "export.zip");
  MockFilePicker.setFiles([
    Services.io
      .newURI(PathUtils.toFileURI(zipFile))
      .QueryInterface(Ci.nsIFileURL).file,
  ]);

  const tab = await new Promise(resolve => {
    const newTab = window.openTab("contentTab", {
      url: "about:import#export",
      onLoad() {
        resolve(newTab);
      },
    });
  });
  await new Promise(resolve => requestAnimationFrame(resolve));
  const importDocument = tab.browser.contentDocument;
  const exportPane = importDocument.getElementById("tabPane-export");
  const cancelButton = exportPane.querySelector(".progressCancel");
  const progressPane = exportPane.querySelector(".progressPane");

  Assert.ok(
    BrowserTestUtils.isHidden(cancelButton),
    "Cancel button should be hidden before exporting"
  );
  EventUtils.synthesizeMouseAtCenter(
    importDocument.getElementById("exportButton"),
    {},
    tab.browser.contentWindow
  );
  await BrowserTestUtils.waitForMutationCondition(
    exportPane,
    { attributes: true },
    () => BrowserTestUtils.isVisible(progressPane)
  );
  Assert.ok(
    BrowserTestUtils.isVisible(cancelButton),
    "Cancel button should be shown while exporting"
  );
  return { tab, zipFile };
}

add_task(async function testProfileExport() {
  const profileDir = await IOUtils.createUniqueDirectory(
    PathUtils.tempDir,
    "profile-tmp"
  );
  const zipFile = PathUtils.join(profileDir, "export.zip");
  const filePath = Services.io
    .newURI(PathUtils.toFileURI(zipFile))
    .QueryInterface(Ci.nsIFileURL);
  MockFilePicker.setFiles([filePath.file]);
  registerCleanupFunction(async () => {
    await IOUtils.remove(profileDir, {
      recursive: true,
    });
  });

  const tab = await new Promise(resolve => {
    const newTab = window.openTab("contentTab", {
      url: "about:import#export",
      onLoad() {
        resolve(newTab);
      },
    });
  });
  await new Promise(resolve => requestAnimationFrame(resolve));

  const importDocument = tab.browser.contentDocument;
  const exportPane = importDocument.getElementById("tabPane-export");

  ok(
    BrowserTestUtils.isVisible(importDocument.getElementById("exportDocs")),
    "Export docs link is visible"
  );
  ok(
    BrowserTestUtils.isHidden(importDocument.getElementById("importDocs")),
    "Import docs link is hidden"
  );
  ok(
    BrowserTestUtils.isVisible(importDocument.getElementById("exportButton")),
    "Export button should be visible at start"
  );
  importDocument.getElementById("exportButton").focus();
  await TestUtils.waitForTick();
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#exportButton",
    {},
    tab.browser
  );
  const progressPane = exportPane.querySelector(".progressPane");
  await BrowserTestUtils.waitForMutationCondition(
    exportPane,
    {
      attributes: true,
    },
    () => BrowserTestUtils.isVisible(progressPane)
  );
  ok(
    BrowserTestUtils.isHidden(importDocument.getElementById("exportButton")),
    "Export button should be hidden while export is in progress"
  );

  const finish = exportPane.querySelector(".progressFinish");
  await BrowserTestUtils.waitForMutationCondition(
    exportPane,
    {
      attributes: true,
    },
    () => BrowserTestUtils.isVisible(finish)
  );
  ok(
    BrowserTestUtils.isVisible(progressPane),
    "When export succeeds and finish is shown, progress is still displayed"
  );

  const tabClosedPromise = BrowserTestUtils.waitForEvent(
    tab.tabNode,
    "TabClose"
  );
  // Using BrowserTestUtils fails, because clicking the button destroys the
  // context, which means the actor from BTU gets destroyed before it can report
  // that it emitted the event.
  await EventUtils.synthesizeMouseAtCenter(
    finish,
    {},
    tab.browser.contentWindow
  );
  await tabClosedPromise;

  const exportZipStat = await IOUtils.stat(zipFile);
  info(exportZipStat.size);
  Assert.greater(exportZipStat.size, 10, "Zip is not empty");
});

add_task(async function testCancelExport() {
  stubSlowArchive();
  const { tab, zipFile } = await startExport();
  const importDocument = tab.browser.contentDocument;
  const exportPane = importDocument.getElementById("tabPane-export");
  const exportButton = importDocument.getElementById("exportButton");
  const cancelButton = exportPane.querySelector(".progressCancel");
  const progressPane = exportPane.querySelector(".progressPane");

  EventUtils.synthesizeMouseAtCenter(
    cancelButton,
    {},
    tab.browser.contentWindow
  );
  await BrowserTestUtils.waitForMutationCondition(
    exportPane,
    { attributes: true },
    () => BrowserTestUtils.isVisible(exportButton)
  );
  Assert.ok(
    BrowserTestUtils.isHidden(progressPane),
    "Progress should be hidden after cancelling"
  );
  Assert.ok(
    BrowserTestUtils.isHidden(cancelButton),
    "Cancel button should be hidden after cancelling"
  );
  Assert.ok(
    !importDocument.querySelector("#errorNotifications notification-message"),
    "Cancelling should not be reported as an error"
  );
  Assert.ok(
    !(await IOUtils.exists(`${zipFile}.part`)),
    "Partial backup should have been removed"
  );
  Assert.ok(
    !(await IOUtils.exists(zipFile)),
    "Should not have left a backup behind"
  );

  document.getElementById("tabmail").closeTab(tab);
  ZipArchive.create = realCreate;
});

add_task(async function testCloseTabCancelsExport() {
  const { aborted } = stubSlowArchive();
  const { tab, zipFile } = await startExport();
  await TestUtils.waitForCondition(
    () => IOUtils.exists(`${zipFile}.part`),
    "Partial backup should be written while exporting"
  );

  const tabClosed = BrowserTestUtils.waitForEvent(tab.tabNode, "TabClose");
  EventUtils.synthesizeMouseAtCenter(
    tab.tabNode.querySelector(".tab-close-button"),
    {},
    window
  );
  await tabClosed;
  await aborted;
  Assert.ok(true, "Closing the tab should have cancelled the export");
  await TestUtils.waitForCondition(
    async () => !(await IOUtils.exists(`${zipFile}.part`)),
    "Partial backup should be removed after closing the tab"
  );
  Assert.ok(
    !(await IOUtils.exists(zipFile)),
    "Should not have left a backup behind"
  );
  ZipArchive.create = realCreate;
});
