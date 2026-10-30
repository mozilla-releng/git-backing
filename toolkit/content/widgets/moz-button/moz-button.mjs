/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// <reference path="./moz-button.tagmap.d.ts" />

import { html, ifDefined, classMap } from "../vendor/lit.all.mjs";
import { MozLitElement } from "../lit-utils.mjs";

window.MozXULElement?.insertFTLIfNeeded("toolkit/global/mozButton.ftl");

// eslint-disable-next-line import/no-unassigned-import
import "chrome://global/content/elements/moz-label.mjs";

/**
 * Controls moz-button behavior when menuId property is set.
 * Helps to integrate moz-button with panel-list.
 */
class MenuController {
  /** @type {MozButton} */
  host;

  /** @type {string | null} */
  #menuId;

  /** @type {HTMLElement | null} */
  #menuEl;

  /** @type {boolean} */
  #hostIsSplitButton;

  /**
   * @param {MozButton} host
   */
  constructor(host) {
    this.host = host;
    host.addController(this);
  }

  hostConnected() {
    this.hostUpdated();
  }

  hostDisconnected() {
    this.removePanelListListeners();
    this.#menuId = null;
    this.#menuEl = null;
  }

  hostUpdated() {
    let hostMenuId = this.host.menuId;
    let hostIsSplitButton = this.host.isSplitButton;

    if (
      this.#menuId === hostMenuId &&
      this.#hostIsSplitButton === hostIsSplitButton
    ) {
      return;
    }
    if (this.#menuEl?.localName == "panel-list") {
      this.panelListCleanUp();
    }

    this.#menuId = hostMenuId;
    this.#hostIsSplitButton = hostIsSplitButton;

    // Check to see if a menuId has been added to host, or changed
    if (this.#menuId) {
      this.#menuEl = this.getPanelList();

      if (this.#menuEl?.localName == "panel-list") {
        this.panelListSetUp();
      }
    }

    // Check to see if menuId has been removed from host
    if (!this.#menuId) {
      this.#menuEl = null;
      this.host.removeController(this);
    }
  }

  /**
   * Retrieves the panel-list element matching the host's menuId.
   *
   * @returns {HTMLElement | null}
   */
  getPanelList() {
    let root = this.host.getRootNode();
    let menuEl = null;

    while (root) {
      menuEl = root.querySelector?.(`#${this.#menuId}`);
      if (menuEl) {
        break;
      }

      if (root instanceof ShadowRoot) {
        root = root.host?.getRootNode();
      } else {
        break;
      }
    }

    return menuEl;
  }

  /**
   * Handles opening/closing the panel-list when the host is clicked or activated via keyboard.
   *
   * @param {MouseEvent|KeyboardEvent} event
   */
  openPanelList = event => {
    if (event.type == "click") {
      event.preventDefault();
    }
    if (
      (event.type == "mousedown" && event.button == 0) ||
      event.inputSource == MouseEvent.MOZ_SOURCE_KEYBOARD ||
      !event.detail
    ) {
      if (this.#hostIsSplitButton) {
        this.#menuEl?.toggle(event, this.host.chevronButtonEl);
      } else {
        this.#menuEl?.toggle(event, this.host);
      }
    }
  };

  /**
   * Listener for shown/hidden that keeps the host's open attribute up to date.
   *
   * @param {CustomEvent} event
   *   The shown or hidden event.
   */
  #updateOpenAttr = event => {
    if (event.type == "shown") {
      this.host.toggleAttribute("open", true);
    } else if (event.type == "hidden") {
      this.host.removeAttribute("open");
    }
  };

  /**
   * Removes event listeners related to panel-list.
   */
  removePanelListListeners() {
    if (this.#hostIsSplitButton) {
      this.host.chevronButtonEl?.removeEventListener(
        "click",
        this.openPanelList
      );
      this.host.chevronButtonEl?.removeEventListener(
        "mousedown",
        this.openPanelList
      );
    } else {
      this.host.removeEventListener("click", this.openPanelList);
      this.host.removeEventListener("mousedown", this.openPanelList);
    }
    this.#menuEl?.removeEventListener("shown", this.#updateOpenAttr);
    this.#menuEl?.removeEventListener("hidden", this.#updateOpenAttr);
  }

  /**
   * Sets up the host for integration with panel-list,
   * adding necessary event listeners and ARIA attributes.
   */
  panelListSetUp() {
    if (this.#hostIsSplitButton) {
      this.host.chevronButtonEl?.addEventListener("click", this.openPanelList);
      this.host.chevronButtonEl?.addEventListener(
        "mousedown",
        this.openPanelList
      );
    } else {
      this.host.addEventListener("click", this.openPanelList);
      this.host.addEventListener("mousedown", this.openPanelList);
    }
    this.#menuEl.addEventListener("shown", this.#updateOpenAttr);
    this.#menuEl.addEventListener("hidden", this.#updateOpenAttr);
    this.host.ariaHasPopup = "menu";
    this.host.ariaExpanded = this.#menuEl.open ? "true" : "false";
    this.host.toggleAttribute("open", this.#menuEl.open);
    let triggerEl = this.#hostIsSplitButton
      ? this.host.chevronButtonEl
      : this.host.buttonEl;
    if (triggerEl) {
      triggerEl.popoverTargetElement = this.#menuEl;
    }
  }

  /**
   * Cleans up panel-list integration,
   * removing event listeners and clearing ARIA attributes.
   */
  panelListCleanUp() {
    this.removePanelListListeners();
    this.host.ariaHasPopup = null;
    this.host.ariaExpanded = null;
    this.host.removeAttribute("open");
    let triggerEl = this.#hostIsSplitButton
      ? this.host.chevronButtonEl
      : this.host.buttonEl;
    if (triggerEl) {
      triggerEl.popoverTargetElement = null;
    }
  }
}

/**
 * A button with multiple types and three sizes.
 *
 * @tagname moz-button
 * @cssproperty [--button-outer-padding-inline] - Used to set the outer inline padding of toolbar style buttons
 * @cssproperty [--button-outer-padding-block] - Used to set the outer block padding of toolbar style buttons.
 * @cssproperty [--button-outer-padding-inline-start] - Used to set the outer inline-start padding of toolbar style buttons
 * @cssproperty [--button-outer-padding-inline-end] - Used to set the outer inline-end padding of toolbar style buttons
 * @cssproperty [--button-outer-padding-block-start] - Used to set the outer block-start padding of toolbar style buttons
 * @cssproperty [--button-outer-padding-block-end] - Used to set the outer block-end padding of toolbar style buttons
 * @slot default - The button's content, overrides label property.
 * @fires click - The click event.
 */
export class MozButton extends MozLitElement {
  static shadowRootOptions = {
    ...MozLitElement.shadowRootOptions,
    delegatesFocus: true,
  };

  static properties = {
    label: { type: String, reflect: true, fluent: true },
    type: { type: String, reflect: true },
    size: { type: String, reflect: true },
    disabled: { type: Boolean, reflect: true },
    title: { type: String, mapped: true },
    tooltipText: { type: String, fluent: true },
    ariaLabel: { type: String, mapped: true },
    ariaHasPopup: { type: String, mapped: true },
    ariaExpanded: { type: String, mapped: true },
    ariaPressed: { type: String, mapped: true },
    ariaChecked: { type: String, mapped: true },
    ariaSelected: { type: String, mapped: true },
    buttonRole: { type: String },
    iconSrc: { type: String },
    hasVisibleLabel: { type: Boolean, state: true },
    accessKey: { type: String, mapped: true },
    attention: { type: Boolean },
    iconPosition: { type: String, reflect: true },
    menuId: { type: String, reflect: true },
    parentDisabled: { type: Boolean },
  };

  static queries = {
    buttonEl: "#main-button",
    chevronButtonEl: "#chevron-button",
    slotEl: "slot",
    backgroundEl: "#main-button .button-background",
  };

  constructor() {
    super();

    /**
     * The button's label, overridden by slotted content.
     *
     * @type {string | undefined}
     */
    this.label = undefined;

    /**
     * The button type. Options: default, primary, destructive, icon, icon
     * ghost, ghost.
     *
     * @type {string}
     */
    this.type = "default";

    /**
     * The button size. Options: default, small, large.
     *
     * @type {string}
     */
    this.size = "default";

    /**
     * The disabled state.
     *
     * @type {boolean}
     */
    this.disabled = false;

    /**
     * The title attribute, mapped onto the inner button.
     *
     * @type {string | undefined}
     */
    this.title = undefined;

    /**
     * Title text used when the title property isn't set.
     *
     * @type {string | undefined}
     */
    this.tooltipText = undefined;

    /**
     * The aria-label attribute, mapped onto the inner button.
     *
     * @type {string | undefined}
     */
    this.ariaLabel = undefined;

    /**
     * The aria-haspopup attribute, mapped onto the inner button.
     *
     * @type {string | undefined}
     */
    this.ariaHasPopup = undefined;

    /**
     * The aria-expanded attribute, mapped onto the inner button.
     *
     * @type {string | undefined}
     */
    this.ariaExpanded = undefined;

    /**
     * The aria-pressed attribute, mapped onto the inner button.
     *
     * @type {string | undefined}
     */
    this.ariaPressed = undefined;

    /**
     * The aria-checked attribute, mapped onto the inner button.
     *
     * @type {string | undefined}
     */
    this.ariaChecked = undefined;

    /**
     * The aria-selected attribute, mapped onto the inner button.
     *
     * @type {string | undefined}
     */
    this.ariaSelected = undefined;

    /**
     * The role attribute of the inner button.
     *
     * @type {string | undefined}
     */
    this.buttonRole = undefined;

    /**
     * Path to the icon displayed in the button.
     *
     * @type {string | undefined}
     */
    this.iconSrc = undefined;

    /**
     * Internal, tracks whether or not the button has a visible label.
     *
     * @type {boolean}
     */
    this.hasVisibleLabel = !!this.label;

    /**
     * The access key, mapped onto the inner button.
     *
     * @type {string | undefined}
     */
    this.accessKey = undefined;

    /**
     * Whether to show a dot notification on the button.
     *
     * @type {boolean}
     */
    this.attention = false;

    /**
     * The icon's position relative to the button label. Options: start, end.
     *
     * @type {string}
     */
    this.iconPosition = "start";

    /**
     * A CSS selector string that identifies the menu element controlled by
     * the button.
     *
     * @type {string}
     */
    this.menuId = "";

    /**
     * Whether the parent of this component is disabled.
     *
     * @type {boolean | undefined}
     */
    this.parentDisabled = undefined;
  }

  updated(changedProperties) {
    super.updated(changedProperties);

    if (changedProperties.has("menuId")) {
      if (this.menuId && !this._menuController) {
        this._menuController = new MenuController(this);
      }
      if (!this.menuId && this._menuController) {
        this._menuController = null;
      }
    }
  }

  /** @internal */
  get isSplitButton() {
    return this.type === "split";
  }

  // Delegate clicks on host to the button element.
  click() {
    this.performUpdate();
    this.buttonEl?.click();
  }

  checkForLabelText() {
    this.hasVisibleLabel = this.slotEl
      ?.assignedNodes()
      .some(node => node.textContent.trim());
  }

  labelTemplate() {
    if (this.label) {
      return html`<span class="text" .textContent=${this.label}></span>`;
    }
    return html`<slot @slotchange=${this.checkForLabelText}></slot>`;
  }

  iconTemplate(position) {
    if (this.iconSrc && position == this.iconPosition) {
      return html`<img src=${this.iconSrc} role="presentation" />`;
    }
    return null;
  }

  chevronButtonTemplate() {
    if (this.isSplitButton) {
      return html`<button
        id="chevron-button"
        size=${this.size}
        ?disabled=${this.disabled || this.parentDisabled}
        data-l10n-id="moz-button-more-options"
        aria-labelledby="main-button chevron-button"
        aria-expanded=${ifDefined(this.ariaExpanded)}
        aria-haspopup=${ifDefined(this.ariaHasPopup)}
        @click=${e => e.stopPropagation()}
        @mousedown=${e => e.stopPropagation()}
      >
        <span
          class="button-background"
          part="chevron-button"
          type=${this.type}
          size=${this.size}
        >
          <img
            src="chrome://global/skin/icons/arrow-down${this.size === "small"
              ? "-12"
              : ""}.svg"
            role="presentation"
          />
        </span>
      </button>`;
    }
    return null;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/content/elements/moz-button.css"
      />
      <button
        id="main-button"
        ?disabled=${this.disabled || this.parentDisabled}
        title=${ifDefined(this.title || this.tooltipText)}
        role=${ifDefined(this.buttonRole)}
        aria-label=${ifDefined(this.ariaLabel)}
        aria-expanded=${ifDefined(
          this.isSplitButton ? undefined : this.ariaExpanded
        )}
        aria-haspopup=${ifDefined(
          this.isSplitButton ? undefined : this.ariaHasPopup
        )}
        aria-pressed=${ifDefined(this.ariaPressed)}
        aria-checked=${ifDefined(this.ariaChecked)}
        aria-selected=${ifDefined(this.ariaSelected)}
        accesskey=${ifDefined(this.accessKey)}
      >
        <span
          class=${classMap({
            labelled: this.label || this.hasVisibleLabel,
            "button-background": true,
            badged:
              (this.iconSrc || this.type.includes("icon")) && this.attention,
          })}
          part="button"
          type=${this.type}
          size=${this.size}
        >
          ${this.iconTemplate("start")}
          <label
            is="moz-label"
            shownaccesskey=${ifDefined(this.accessKey)}
            part="moz-button-label"
          >
            ${this.labelTemplate()}
          </label>
          ${this.iconTemplate("end")}
        </span>
      </button>
      ${this.chevronButtonTemplate()}
    `;
  }
}
customElements.define("moz-button", MozButton);
