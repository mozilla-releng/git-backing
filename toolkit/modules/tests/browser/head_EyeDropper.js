/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Aliased, as ESLint knows of the EyeDropper Web API as a browser global.
const { EyeDropper: EyeDropperOverlay, getActiveEyeDropperForTests } =
  ChromeUtils.importESModule("moz-src:///toolkit/modules/EyeDropper.sys.mjs");

const RED = { r: 255, g: 0, b: 0 };
const GREEN = { r: 0, g: 200, b: 0 };
const BLUE = { r: 0, g: 128, b: 255 };
const YELLOW = { r: 255, g: 200, b: 0 };

// The page is split in four at 200px from the left and from the top: red at the
// top left, green at the top right, blue at the bottom left and yellow at the
// bottom right.
const PAGE_URL =
  "data:text/html," +
  encodeURIComponent(`<!doctype html>
  <body style="margin:0; display:grid; grid-template: 200px 1fr / 200px 1fr;
               height:100vh">
    <div style="background:rgb(255, 0, 0)"></div>
    <div style="background:rgb(0, 200, 0)"></div>
    <div style="background:rgb(0, 128, 255)"></div>
    <div style="background:rgb(255, 200, 0)"></div>
  </body>`);

function getAnonymousContentHosts(aDoc) {
  const container = InspectorUtils.getChildrenForNode(
    aDoc.documentElement,
    /* showingAnonymousContent */ true,
    /* includeAssignedNodes */ false
  ).find(node => node.classList?.contains("moz-custom-content-container"));
  return container ? [...container.children] : [];
}

async function waitForEyeDropperAnonymousContent(aDoc) {
  let eyeDropper = null;
  await TestUtils.waitForCondition(() => {
    let nodes = getAnonymousContentHosts(aDoc);
    for (let node of nodes) {
      eyeDropper = node.openOrClosedShadowRoot?.querySelector(".eyedropper");
      if (eyeDropper) {
        return true;
      }
    }
    return false;
  }, "Wait for EyeDropper AnonymousContent");
  return eyeDropper;
}

/**
 * Waits for the magnifier to be shown and returns where its center is, in CSS
 * pixels relative to the browser window.
 *
 * @param {Element} aEyeDropper The root element of the eyedropper.
 * @returns {Promise<{x: number, y: number}>}
 */
async function getMagnifierCenter(aEyeDropper) {
  await TestUtils.waitForCondition(
    () => !aEyeDropper.hidden,
    "Wait for the magnifier to be shown"
  );
  const rect = aEyeDropper
    .querySelector(".eyedropper-canvas")
    .getBoundingClientRect();
  return { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
}

/**
 * Opens the eyedropper over the page shown in the browser and waits for it to
 * have drawn the page screenshot.
 *
 * @param {XULBrowserElement} aBrowser
 * @param {object} [aOptions] Passed to EyeDropper.pick().
 * @returns {Promise<{picked: Promise}>} The promise returned by pick(), wrapped
 *          so that it is not awaited along.
 */
async function openEyeDropper(aBrowser, aOptions) {
  const pickedResult = new EyeDropperOverlay(aBrowser).pick(aOptions);
  const eyeDropperAnonymousContent = await waitForEyeDropperAnonymousContent(
    aBrowser.ownerDocument
  );
  return {
    eyeDropperAnonymousContent,
    pickedResult,
  };
}

/**
 * Records the input events the page receives.
 *
 * @param {XULBrowserElement} aBrowser
 */
function watchPageInput(aBrowser) {
  return SpecialPowers.spawn(aBrowser, [], () => {
    content.__seen = [];
    for (const type of [
      "mousemove",
      "mousedown",
      "mouseup",
      "click",
      "keydown",
      "keyup",
    ]) {
      content.addEventListener(type, () => content.__seen.push(type), true);
    }
  });
}

/**
 * @param {XULBrowserElement} aBrowser
 * @returns {Promise<string[]>} The input events seen since watchPageInput().
 */
function readPageInput(aBrowser) {
  return SpecialPowers.spawn(aBrowser, [], () => content.__seen.slice());
}
