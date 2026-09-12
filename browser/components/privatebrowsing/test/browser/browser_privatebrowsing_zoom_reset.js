/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Resetting the zoom level in a private browsing window must stick across tab
// switches even when the site has a non-default zoom level saved from normal
// browsing, and must not clear that saved normal browsing zoom level.
// See bug 2071119.

const TEST_PAGE_URL = "https://example.org/";
const SAVED_ZOOM = 1.5;
const CUSTOM_ZOOM = 1.2;

const gContentPrefs = Cc["@mozilla.org/content-pref/service;1"].getService(
  Ci.nsIContentPrefService2
);

function getZoomPref(loadContext) {
  return new Promise(resolve => {
    let value;
    gContentPrefs.getByDomainAndName(
      TEST_PAGE_URL,
      FullZoom.name,
      loadContext,
      {
        handleResult(pref) {
          value = pref.value;
        },
        handleCompletion() {
          resolve(value);
        },
      }
    );
  });
}

// Filter on the browser itself: the notification fires for every location
// change in the window, so a still-pending one from another tab would satisfy
// the wait before FullZoom has re-applied the zoom level we care about.
function promiseLocationChange(browser) {
  return TestUtils.topicObserved(
    "browser-fullZoom:location-change",
    subject => subject == browser
  );
}

add_setup(async function () {
  registerCleanupFunction(async function () {
    await new Promise(resolve =>
      gContentPrefs.removeByName(FullZoom.name, null, {
        handleCompletion: () => resolve(),
      })
    );
  });
});

add_task(async function test_reset_in_private_window() {
  info("Save a non-default zoom level for the site in normal browsing.");
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    TEST_PAGE_URL
  );
  FullZoom.setZoom(SAVED_ZOOM, tab.linkedBrowser);
  await TestUtils.waitForCondition(
    async () => (await getZoomPref(Cu.createLoadContext())) == SAVED_ZOOM,
    "Waiting for the normal browsing zoom level to be saved"
  );
  BrowserTestUtils.removeTab(tab);

  let win = await BrowserTestUtils.openNewBrowserWindow({ private: true });
  let privateTab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    TEST_PAGE_URL
  );

  await TestUtils.waitForCondition(
    () => win.ZoomManager.zoom == SAVED_ZOOM,
    "Waiting for the private browsing tab to pick up the saved zoom level"
  );

  info("Reset the zoom level in the private browsing window.");
  await win.FullZoom.reset();
  is(win.ZoomManager.zoom, 1, "Zoom level was reset to the default");

  info("Switch away from the tab and back again.");
  let otherTab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    "about:blank"
  );
  let locationChange = promiseLocationChange(privateTab.linkedBrowser);
  await BrowserTestUtils.switchTab(win.gBrowser, privateTab);
  await locationChange;

  is(
    win.ZoomManager.zoom,
    1,
    "Zoom level is still the default after switching back"
  );

  is(
    await getZoomPref(Cu.createLoadContext()),
    SAVED_ZOOM,
    "The normal browsing zoom level was not cleared by the private window"
  );

  BrowserTestUtils.removeTab(otherTab);
  BrowserTestUtils.removeTab(privateTab);
  await BrowserTestUtils.closeWindow(win);
});

// Picking a custom zoom level in a private browsing window already stuck,
// because that path stores a value rather than removing one. Guard it, so the
// two halves of the behaviour stay covered together.
add_task(async function test_custom_zoom_in_private_window() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    TEST_PAGE_URL
  );
  FullZoom.setZoom(SAVED_ZOOM, tab.linkedBrowser);
  await TestUtils.waitForCondition(
    async () => (await getZoomPref(Cu.createLoadContext())) == SAVED_ZOOM,
    "Waiting for the normal browsing zoom level to be saved"
  );
  BrowserTestUtils.removeTab(tab);

  let win = await BrowserTestUtils.openNewBrowserWindow({ private: true });
  let privateTab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    TEST_PAGE_URL
  );

  info("Pick a custom zoom level in the private browsing window.");
  win.FullZoom.setZoom(CUSTOM_ZOOM, privateTab.linkedBrowser);
  await TestUtils.waitForCondition(
    () => win.ZoomManager.zoom == CUSTOM_ZOOM,
    "Waiting for the custom zoom level to be applied"
  );

  let otherTab = await BrowserTestUtils.openNewForegroundTab(
    win.gBrowser,
    "about:blank"
  );
  let locationChange = promiseLocationChange(privateTab.linkedBrowser);
  await BrowserTestUtils.switchTab(win.gBrowser, privateTab);
  await locationChange;

  is(
    win.ZoomManager.zoom,
    CUSTOM_ZOOM,
    "Custom zoom level survived switching back"
  );
  is(
    await getZoomPref(Cu.createLoadContext()),
    SAVED_ZOOM,
    "The normal browsing zoom level was not changed by the private window"
  );

  BrowserTestUtils.removeTab(otherTab);
  BrowserTestUtils.removeTab(privateTab);
  await BrowserTestUtils.closeWindow(win);
});
