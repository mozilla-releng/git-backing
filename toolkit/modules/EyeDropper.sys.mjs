/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const XHTML_NS = "http://www.w3.org/1999/xhtml";
const STYLESHEET_URI = "chrome://global/skin/eyedropper.css";

// How many times the page is magnified.
const ZOOM_LEVEL = 6;

// There is at most one eyedropper per browser window, the one opened last
// replacing any other.
const gActiveEyeDroppers = new WeakMap();

const DEBUG = true;
function debug(aStr) {
  if (DEBUG) {
    dump("-*- EyeDropper: " + aStr + "\n");
  }
}

/**
 * The eyedropper currently open in the given browser window, if any. Tests use
 * it to wait for the page screenshot to have been drawn before sampling.
 *
 * @param {Window} aWindow A browser window.
 * @returns {EyeDropper | undefined} Current active EyeDropper if any.
 */
export function getActiveEyeDropperForTests(aWindow) {
  return gActiveEyeDroppers.get(aWindow);
}

/**
 * An eyedropper shown in the browser window's document, over the page shown in
 * a <browser>. It works on a screenshot of the page taken in the parent
 * process, which includes out-of-process iframes and never leaves the parent
 * process: only the picked color is reported.
 *
 * This mimics the DevTools eyedropper highlighter: a circular magnifier that
 * follows the cursor and highlights the pixel under it, with a label showing
 * that pixel's color.
 */
export class EyeDropper {
  /** @type {XULBrowserElement} */
  #browser;
  /** @type {AnonymousContent} */
  #anonymousContent = null;
  /** The .eyedropper element, holding the magnifier and its label. */
  #root = null;
  /** @type {HTMLCanvasElement} The magnifier. */
  #canvas = null;
  /** The element showing the color under the cursor as text. */
  #colorText = null;
  /** The size of the magnifier, in CSS pixels, as set by the stylesheet. */
  #magnifierSize = 0;
  /** @type {ImageBitmap} */
  #screenshot = null;
  /** The position of the page in the window, in CSS pixels. */
  #pageRect = null;
  /** The position of the cursor in the page, in CSS pixels. */
  #cursorX = 0;
  #cursorY = 0;
  /** @type {{r: number, g: number, b: number}} The color under the cursor. */
  #cursorColor = null;
  #inputAbortController = new AbortController();
  #resolvePick = null;

  /**
   * Whether the magnifier has been drawn from the page screenshot.
   */
  drawn = false;

  /**
   * @param {XULBrowserElement} aBrowser
   */
  constructor(aBrowser) {
    this.#browser = aBrowser;
  }

  get #window() {
    return this.#browser.documentGlobal;
  }

  /**
   * Shows the eyedropper over the page shown in the browser and resolves with
   * the picked color, or with null if the user dismissed it without picking.
   * While it is shown, the page does not receive any mouse or keyboard input.
   *
   * @param {object} [options]
   * @param {AbortSignal} [options.signal] Dismisses the eyedropper, which then
   *        resolves with null.
   * @returns {Promise<{r: number, g: number, b: number} | null>} The picked
   *          color, each component in the 0..255 range, or null.
   */
  async pick({ signal } = {}) {
    try {
      return await this.#pick(signal);
    } finally {
      this.#destroy();
    }
  }

  async #pick(signal) {
    if (signal?.aborted) {
      return null;
    }
    const { promise, resolve } = Promise.withResolvers();
    this.#resolvePick = resolve;
    signal?.addEventListener("abort", () => this.#finish(null), {
      once: true,
    });

    const oldEyeDropper = gActiveEyeDroppers.get(this.#window);
    if (oldEyeDropper) {
      debug("Dismiss existing EyeDropper");
      oldEyeDropper.#finish(null);
    }
    gActiveEyeDroppers.set(this.#window, this);

    this.#pageRect = this.#browser.getBoundingClientRect();
    const browsingContext = this.#browser.browsingContext;
    // Rendered at the full zoom so that one pixel of the snapshot is one CSS
    // pixel of the browser window.
    this.#screenshot = await browsingContext.currentWindowGlobal.drawSnapshot(
      null,
      browsingContext.fullZoom,
      "rgb(255, 255, 255)"
    );
    if (!this.#resolvePick) {
      return promise;
    }

    await this.#buildMarkup();
    if (!this.#resolvePick) {
      return promise;
    }

    this.#addInputListeners();
    const { x, y } = this.#initialPosition;
    this.#moveTo(x, y);
    this.#root.hidden = false;
    return promise;
  }

  /**
   * Where the cursor last was over the page, or the middle of the page if it
   * is elsewhere, e.g. when the eyedropper was opened from the keyboard.
   *
   * @returns {{x: number, y: number}} In CSS pixels, relative to the window.
   */
  get #initialPosition() {
    const x = {};
    const y = {};
    this.#window.windowUtils.getLastOverWindowPointerLocationInCSSPixels(x, y);
    if (this.#isOverPage(x.value, y.value)) {
      return { x: x.value, y: y.value };
    }
    const { left, top, width, height } = this.#pageRect;
    return { x: left + width / 2, y: top + height / 2 };
  }

  #destroy() {
    this.#finish(null);
    this.#inputAbortController.abort();
    if (this.#anonymousContent) {
      this.#browser.ownerDocument.removeAnonymousContent(
        this.#anonymousContent
      );
      this.#anonymousContent = null;
    }
    this.#screenshot?.close();
    this.#screenshot = null;
  }

  #finish(color) {
    if (gActiveEyeDroppers.get(this.#window) === this) {
      gActiveEyeDroppers.delete(this.#window);
    }
    const resolvePick = this.#resolvePick;
    this.#resolvePick = null;
    resolvePick?.(color);
  }

  async #buildMarkup() {
    const doc = this.#browser.ownerDocument;
    this.#anonymousContent = doc.insertAnonymousContent();
    const createElement = (tagName, className, parent) => {
      const element = doc.createElementNS(XHTML_NS, tagName);
      if (className) {
        element.className = className;
      }
      parent.appendChild(element);
      return element;
    };

    const shadowRoot = this.#anonymousContent.root;
    const link = createElement("link", null, shadowRoot);
    link.rel = "stylesheet";
    link.href = STYLESHEET_URI;
    const stylesheetLoaded = new Promise(resolve => {
      link.addEventListener("load", resolve, { once: true });
      link.addEventListener("error", resolve, { once: true });
    });

    // Clips the eyedropper to the page, so that it is not drawn over the
    // browser UI.
    const viewport = createElement("div", "eyedropper-viewport", shadowRoot);
    for (const name of ["left", "top", "width", "height"]) {
      viewport.style.setProperty(
        `--eyedropper-page-${name}`,
        `${this.#pageRect[name]}px`
      );
    }

    this.#root = createElement("div", "eyedropper", viewport);
    this.#root.hidden = true;

    this.#canvas = createElement("canvas", "eyedropper-canvas", this.#root);
    const label = createElement("div", "eyedropper-label", this.#root);
    createElement("div", "eyedropper-color-preview", label);
    this.#colorText = createElement("div", "eyedropper-color-value", label);

    await stylesheetLoaded;

    this.#magnifierSize = parseFloat(
      this.#window.getComputedStyle(this.#canvas).width
    );
    // Sized in device pixels, so that the magnifier is sharp.
    this.#canvas.width = this.#canvas.height = Math.round(
      this.#magnifierSize * this.#window.devicePixelRatio
    );
    const context = this.#canvas.getContext("2d", { willReadFrequently: true });
    context.imageSmoothingEnabled = false;
  }

  /**
   * Moves the magnifier to the given position, clamped to the page.
   *
   * @param {number} x In CSS pixels, relative to the window.
   * @param {number} y In CSS pixels, relative to the window.
   */
  #moveTo(x, y) {
    const { left, top, width, height } = this.#pageRect;
    const clamp = (value, max) => Math.min(Math.max(Math.floor(value), 0), max);
    this.#cursorX = clamp(x - left, Math.ceil(width) - 1);
    this.#cursorY = clamp(y - top, Math.ceil(height) - 1);

    this.#drawMagnifier();

    this.#root.style.setProperty("--eyedropper-x", `${this.#cursorX}px`);
    this.#root.style.setProperty("--eyedropper-y", `${this.#cursorY}px`);

    // Keep the label inside the page.
    const magnifierSize = this.#magnifierSize;
    this.#root.toggleAttribute(
      "label-above",
      this.#cursorY >= height - magnifierSize
    );
    this.#root.toggleAttribute("label-right", this.#cursorX <= magnifierSize);
    this.#root.toggleAttribute(
      "label-left",
      this.#cursorX > magnifierSize && this.#cursorX >= width - magnifierSize
    );
  }

  /**
   * Draws the magnified area around the cursor, with a grid showing the pixel
   * boundaries and a box around the pixel under the cursor, and updates the
   * label with the color of that pixel.
   */
  #drawMagnifier() {
    // `ctx.canvas` is null for a canvas in anonymous content, so the canvas is
    // kept around instead.
    const ctx = this.#canvas.getContext("2d");
    const canvasSize = this.#canvas.width;
    // An even number of page pixels is shown across, the one under the cursor
    // being right after the middle.
    let pixelsAcross = Math.ceil(this.#magnifierSize / ZOOM_LEVEL);
    pixelsAcross += pixelsAcross % 2;
    const centerPixel = Math.floor(pixelsAcross / 2);

    ctx.fillStyle = "white";
    ctx.fillRect(0, 0, canvasSize, canvasSize);
    ctx.drawImage(
      this.#screenshot,
      this.#cursorX - centerPixel,
      this.#cursorY - centerPixel,
      pixelsAcross,
      pixelsAcross,
      0,
      0,
      canvasSize,
      canvasSize
    );

    // The size of one magnified page pixel in the canvas, and where the one
    // under the cursor starts.
    const pixelSize = canvasSize / pixelsAcross;
    const center = centerPixel * pixelSize;
    const [r, g, b] = ctx.getImageData(
      center + pixelSize / 2,
      center + pixelSize / 2,
      1,
      1
    ).data;
    this.#cursorColor = { r, g, b };

    ctx.lineWidth = 1;
    ctx.strokeStyle = "rgba(143, 143, 143, 0.2)";
    ctx.beginPath();
    for (let i = 0; i <= canvasSize; i += pixelSize) {
      ctx.moveTo(i - 0.5, 0);
      ctx.lineTo(i - 0.5, canvasSize);
      ctx.moveTo(0, i - 0.5);
      ctx.lineTo(canvasSize, i - 0.5);
    }
    ctx.stroke();

    ctx.lineJoin = "miter";
    ctx.strokeStyle = "black";
    ctx.strokeRect(center - 1.5, center - 1.5, pixelSize + 2, pixelSize + 2);
    ctx.strokeStyle = "white";
    ctx.strokeRect(center - 0.5, center - 0.5, pixelSize, pixelSize);

    const hex =
      "#" + [r, g, b].map(c => c.toString(16).padStart(2, "0")).join("");
    this.#root.style.setProperty("--eyedropper-color", hex);
    this.#colorText.textContent = hex;
    this.drawn = true;
  }

  /**
   * @param {number} x In CSS pixels, relative to the window.
   * @param {number} y In CSS pixels, relative to the window.
   */
  #isOverPage(x, y) {
    const { left, top, right, bottom } = this.#pageRect;
    return x >= left && x < right && y >= top && y < bottom;
  }

  /**
   * Input is taken from the window, before it reaches either the <browser> or
   * the magnifier, which is centered on the cursor, and swallowed so that the
   * page never reacts to it. Doing it in the parent process also covers
   * out-of-process iframes, whose events the page's own process never sees.
   * Preventing the default from chrome also stops the event from being
   * forwarded to the content process.
   */
  #addInputListeners() {
    const window = this.#window;
    const options = {
      capture: true,
      signal: this.#inputAbortController.signal,
    };
    const swallow = event => {
      event.preventDefault();
      event.stopPropagation();
    };
    const cancel = () => this.#finish(null);

    window.addEventListener(
      "mousemove",
      event => {
        if (this.#isOverPage(event.clientX, event.clientY)) {
          swallow(event);
          this.#moveTo(event.clientX, event.clientY);
        }
      },
      options
    );
    // Pressing a mouse button over the browser UI dismisses the eyedropper
    // and lets the UI handle it.
    window.addEventListener(
      "mousedown",
      event => {
        if (!this.#isOverPage(event.clientX, event.clientY)) {
          cancel();
          return;
        }
        swallow(event);
      },
      options
    );
    window.addEventListener(
      "click",
      event => {
        if (this.#isOverPage(event.clientX, event.clientY)) {
          swallow(event);
          if (event.button === 0) {
            this.#moveTo(event.clientX, event.clientY);
            this.#finish(this.#cursorColor);
          }
        }
      },
      options
    );
    for (const type of ["mouseup", "dblclick", "auxclick", "contextmenu"]) {
      window.addEventListener(
        type,
        event => {
          if (this.#isOverPage(event.clientX, event.clientY)) {
            swallow(event);
          }
        },
        options
      );
    }
    // Scrolling is done off the main thread, where it cannot be prevented from
    // here, and would make the screenshot stale.
    window.addEventListener(
      "wheel",
      event => {
        if (this.#isOverPage(event.clientX, event.clientY)) {
          swallow(event);
        }
      },
      options
    );
    // Keys go to the focused <browser> but are seen by the window first.
    // Shortcuts with modifiers dismiss the eyedropper, so that they apply to
    // the page as it is.
    window.addEventListener(
      "keydown",
      event => {
        if (event.metaKey || event.ctrlKey || event.altKey) {
          cancel();
          return;
        }
        swallow(event);
        if (event.key === "Escape") {
          cancel();
        }
      },
      options
    );
    for (const type of ["keyup", "keypress"]) {
      window.addEventListener(type, swallow, options);
    }
    // The screenshot no longer matches what is shown.
    this.#browser.addEventListener("FullZoomChange", cancel, options);
    window.addEventListener("resize", cancel, options);
    window.addEventListener("TabSelect", cancel, options);
    window.addEventListener("deactivate", cancel, options);
  }
}
