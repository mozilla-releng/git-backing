/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const PAGE = "browser_registerProtocolHandler_relative.html";

async function checkCounts(expected) {
  await Services.fog.testFlushAllChildren();
  for (let [label, count] of Object.entries(expected)) {
    is(
      Glean.dom.registerProtocolHandlerFrame[label].testGetValue(),
      count,
      `${label} count`
    );
  }
}

add_setup(async function () {
  Services.fog.testResetFOG();
});

add_task(async function testFrameLabels() {
  await BrowserTestUtils.withNewTab(
    pageURL("https://example.com", PAGE),
    async browser => {
      await checkCounts({
        top_level: 1,
        same_origin_iframe: null,
        cross_origin_iframe: null,
      });

      await addIframe(browser, pageURL("https://example.com", PAGE));
      await checkCounts({
        top_level: 1,
        same_origin_iframe: 1,
        cross_origin_iframe: null,
      });

      info("A same-site iframe from another origin counts as cross-origin.");
      await addIframe(browser, pageURL("https://www.example.com", PAGE));
      await checkCounts({
        top_level: 1,
        same_origin_iframe: 1,
        cross_origin_iframe: 1,
      });

      await addIframe(browser, pageURL("https://example.org", PAGE));
      await checkCounts({
        top_level: 1,
        same_origin_iframe: 1,
        cross_origin_iframe: 2,
      });
    }
  );
});
