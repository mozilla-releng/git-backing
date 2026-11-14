/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

// audio_thread_priority loads avrt.dll with LoadLibraryW on AudioIPC callback
// threads after the sandbox has started (bug 2092043).
add_task(async function test_load_avrt_in_new_content_process() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "https://example.com", forceNewProcess: true },
    async browser => {
      const error = await SpecialPowers.spawn(browser, [], () => {
        const { ctypes } = ChromeUtils.importESModule(
          "resource://gre/modules/ctypes.sys.mjs"
        );
        try {
          ctypes.open("avrt.dll").close();
        } catch (e) {
          return String(e);
        }
        return null;
      });
      Assert.equal(error, null, "avrt.dll loaded in content process");
    }
  );
});
