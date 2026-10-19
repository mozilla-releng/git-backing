/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_EyeDropper.js",
  this
);

/**
 * Waits for the magnifier to be shown and checks that it is centered on the
 * given position in the page, allowing for it being rounded to a whole pixel.
 *
 * @param {Element} aEyeDropper The root element of the eyedropper.
 * @param {XULBrowserElement} aBrowser The browser the eyedropper is shown over.
 * @param {number} [aOffsetX] The expected horizontal position, in CSS pixels
 *        from the left of the browser. Defaults to the middle of the browser.
 * @param {number} [aOffsetY] The expected vertical position, in CSS pixels
 *        from the top of the browser. Defaults to the middle of the browser.
 */
async function checkMagnifierPosition(
  aEyeDropper,
  aBrowser,
  aOffsetX,
  aOffsetY
) {
  let magnifierCenter = await getMagnifierCenter(aEyeDropper);
  const browserRect = aBrowser.getBoundingClientRect();
  aOffsetX ??= browserRect.width / 2;
  aOffsetY ??= browserRect.height / 2;
  // The position is rounded down to a whole pixel.
  Assert.lessOrEqual(
    Math.abs(magnifierCenter.x - (browserRect.left + aOffsetX)),
    1,
    "The magnifier is horizontally centered on the cursor"
  );
  Assert.lessOrEqual(
    Math.abs(magnifierCenter.y - (browserRect.top + aOffsetY)),
    1,
    "The magnifier is vertically centered on the cursor"
  );
}

/**
 * Waits for the magnifier to be shown and checks that both the label and the
 * magnifier show the given color, i.e. the color under the cursor.
 *
 * @param {Element} aEyeDropper The root element of the eyedropper.
 * @param {{r: number, g: number, b: number}} aColor The expected color, each
 *        component in the 0..255 range.
 */
async function checkEyeDropperDisplayedColor(aEyeDropper, aColor) {
  const toHexString = ({ r, g, b }) => {
    return "#" + [r, g, b].map(c => c.toString(16).padStart(2, "0")).join("");
  };
  await TestUtils.waitForCondition(
    () => !aEyeDropper.hidden,
    "Wait for the magnifier to be shown"
  );
  const colorValue = aEyeDropper.querySelector(".eyedropper-color-value");
  const hex = toHexString(aColor);
  is(colorValue.textContent, hex, `The label shows the color on the cursor`);
  is(
    aEyeDropper.style.getPropertyValue("--eyedropper-color"),
    hex,
    `The magnifier shows the color on the cursor`
  );
}

add_task(async function open_with_already_aborted_signal() {
  await BrowserTestUtils.withNewTab(PAGE_URL, async browser => {
    const pickedResult = new EyeDropperOverlay(browser).pick({
      signal: AbortSignal.abort(),
    });

    is(await pickedResult, null, "Nothing was picked");
    ok(!getActiveEyeDropperForTests(window), "The eyedropper is not opened");
  });
});

add_task(async function one_eyedropper_per_window() {
  await BrowserTestUtils.withNewTab(PAGE_URL, async browser => {
    const { pickedResult: firstResult } = await openEyeDropper(browser);
    const firstEyeDropper = getActiveEyeDropperForTests(window);
    ok(firstEyeDropper, "The eyedropper is opened");

    const { pickedResult: secondResult } = await openEyeDropper(browser);
    is(
      await firstResult,
      null,
      "Opening another eyedropper dismisses the first one"
    );
    const secondEyeDropper = getActiveEyeDropperForTests(window);
    isnot(firstEyeDropper, secondEyeDropper, "Should open different instance");

    EventUtils.synthesizeKey("KEY_Escape");

    is(await secondResult, null, "Nothing was picked");
  });
});

add_task(async function eyedropper_initial_position() {
  await BrowserTestUtils.withNewTab(PAGE_URL, async browser => {
    info("Open the eyedropper with the cursor over the page");
    EventUtils.synthesizeMouse(browser, 300, 150, { type: "mousemove" });
    let { eyeDropperAnonymousContent, pickedResult } =
      await openEyeDropper(browser);

    info("Check magnifier position");
    await checkMagnifierPosition(eyeDropperAnonymousContent, browser, 300, 150);

    EventUtils.synthesizeKey("KEY_Escape");
    is(await pickedResult, null, "Nothing was picked");

    info("Open the eyedropper with the cursor over the browser UI");
    EventUtils.synthesizeMouseAtCenter(document.getElementById("nav-bar"), {
      type: "mousemove",
    });
    ({ eyeDropperAnonymousContent, pickedResult } =
      await openEyeDropper(browser));

    info("Check magnifier position");
    await checkMagnifierPosition(eyeDropperAnonymousContent, browser);

    EventUtils.synthesizeKey("KEY_Escape");
    is(await pickedResult, null, "Nothing was picked");
  });
});

add_task(async function eyedropper_update_with_mousemove() {
  await BrowserTestUtils.withNewTab(PAGE_URL, async browser => {
    EventUtils.synthesizeMouse(browser, 100, 100, { type: "mousemove" });

    const { eyeDropperAnonymousContent, pickedResult } =
      await openEyeDropper(browser);

    info("check the displayed color on initial position");
    await checkMagnifierPosition(eyeDropperAnonymousContent, browser, 100, 100);
    await checkEyeDropperDisplayedColor(eyeDropperAnonymousContent, RED);

    for (const [x, y, color] of [
      [300, 100, GREEN],
      [100, 300, BLUE],
      [300, 300, YELLOW],
      [100, 100, RED],
    ]) {
      info(`Move cursor to (${x}, ${y}) and check the displayed color`);
      EventUtils.synthesizeMouse(browser, x, y, { type: "mousemove" });
      await checkMagnifierPosition(eyeDropperAnonymousContent, browser, x, y);
      await checkEyeDropperDisplayedColor(eyeDropperAnonymousContent, color);
    }

    EventUtils.synthesizeKey("KEY_Escape");
    is(await pickedResult, null, "Nothing was picked");
  });
});

add_task(async function eyedropper_pick_with_mouse() {
  await BrowserTestUtils.withNewTab(PAGE_URL, async browser => {
    await watchPageInput(browser);
    const { eyeDropperAnonymousContent, pickedResult } =
      await openEyeDropper(browser);

    EventUtils.synthesizeMouse(browser, 100, 100, { type: "mousemove" });
    await checkMagnifierPosition(eyeDropperAnonymousContent, browser, 100, 100);
    await checkEyeDropperDisplayedColor(eyeDropperAnonymousContent, RED);

    EventUtils.synthesizeMouse(browser, 300, 300, { type: "mousemove" });
    EventUtils.synthesizeMouse(browser, 300, 300, {});
    Assert.deepEqual(
      await pickedResult,
      YELLOW,
      "picked the color under the cursor"
    );
    ok(!getActiveEyeDropperForTests(window), "The eyedropper should be closed");
    Assert.deepEqual(
      await readPageInput(browser),
      [],
      "The page did not see the interaction"
    );
  });
});
