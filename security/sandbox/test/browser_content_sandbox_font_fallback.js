/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

// Lao isn't covered by our common fallback fonts, so we use DirectWrite's
// system font fallback, which loads TextShaping.dll after the sandbox has
// started (bug 2092043).
add_task(async function test_dwrite_font_fallback_in_new_content_process() {
  const url =
    "data:text/html;charset=utf-8,<select><option>ພາສາລາວ</option></select>";

  await BrowserTestUtils.withNewTab(
    { gBrowser, url, forceNewProcess: true },
    async browser => {
      const width = await SpecialPowers.spawn(browser, [], () => {
        return content.document.querySelector("select").getBoundingClientRect()
          .width;
      });
      Assert.greater(width, 0, "Select with Lao option was laid out");
    }
  );
});
