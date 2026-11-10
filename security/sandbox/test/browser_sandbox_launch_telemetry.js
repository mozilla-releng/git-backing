/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_sandboxed_launch_recorded() {
  Services.fog.testResetFOG();

  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: "https://example.com/",
    forceNewProcess: true,
  });
  BrowserTestUtils.removeTab(tab);

  // The tab may have used a preallocated process, in which case the launch we
  // see is the asynchronous launch of its replacement.
  await TestUtils.waitForCondition(
    () => Glean.sandbox.launchAttempts.tab.testGetValue() >= 1,
    "Sandboxed content process launch should be counted"
  );
  Assert.equal(
    Glean.sandbox.launchFailures.tab.testGetValue(),
    null,
    "No sandboxed content process launch failures should be counted"
  );
  Assert.equal(
    Glean.sandbox.launchFailed.testGetValue(),
    null,
    "No launch_failed events should be recorded"
  );
});
