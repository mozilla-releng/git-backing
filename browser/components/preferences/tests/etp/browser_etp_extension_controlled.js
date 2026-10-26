/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
AddonTestUtils.initMochitest(this);

const CAT_PREF = "browser.contentblocking.category";
const TP_PREF = "privacy.trackingprotection.enabled";
const EXTENSION_ID = "@set_tp";
const EXTENSION_NAME = "set_tp";
const CONTROLLED_L10N_ID =
  "extension-controlling-websites-content-blocking-all-trackers";

function getMessageBar(control) {
  return control.querySelector(".extension-controlled-message-bar");
}

async function waitForMessageBar(control, shown) {
  await BrowserTestUtils.waitForMutationCondition(
    control,
    { childList: true, subtree: true },
    () => !!getMessageBar(control) == shown
  );
  await control.updateComplete;
}

function assertMessageBar(control) {
  let messageBar = getMessageBar(control);
  ok(messageBar, `${control.id} shows the extension controlled message bar`);
  is(
    messageBar.messageL10nId,
    CONTROLLED_L10N_ID,
    "Message bar uses the tracking protection l10n id"
  );
  is(
    messageBar.messageL10nArgs.name,
    EXTENSION_NAME,
    "Message bar names the controlling extension"
  );
  return messageBar;
}

function loadTPExtension() {
  return ExtensionTestUtils.loadExtension({
    useAddonManager: "permanent",
    manifest: {
      name: EXTENSION_NAME,
      browser_specific_settings: { gecko: { id: EXTENSION_ID } },
      permissions: ["privacy"],
    },
    background() {
      browser.privacy.websites.trackingProtectionMode.set({ value: "always" });
    },
  });
}

add_task(async function test_etp_extension_controlled() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [CAT_PREF, "standard"],
      [TP_PREF, false],
    ],
  });

  let { doc } = await openEtpPage();
  let radioGroupControl = doc.getElementById(
    "setting-control-contentBlockingCategoryRadioGroup"
  );
  let standardRadio = getControl(doc, "etpLevelStandard");
  let strictRadio = getControl(doc, "etpLevelStrict");
  let customRadio = getControl(doc, "etpLevelCustom");
  let customizeButton = getControl(doc, "etpCustomizeButton");

  ok(!getMessageBar(radioGroupControl), "No message bar before install");
  ok(!standardRadio.disabled, "Standard radio enabled before install");
  ok(!strictRadio.disabled, "Strict radio enabled before install");

  let extension = loadTPExtension();
  let messageBarShown = waitForMessageBar(radioGroupControl, true);
  await extension.startup();
  await messageBarShown;

  ok(Services.prefs.getBoolPref(TP_PREF), "Extension enabled tracking prot.");
  let messageBar = assertMessageBar(radioGroupControl);
  ok(standardRadio.disabled, "Standard radio disabled while controlled");
  ok(strictRadio.disabled, "Strict radio disabled while controlled");
  ok(!customRadio.disabled, "Custom radio stays enabled while controlled");

  await TestUtils.waitForCondition(
    () => customRadio.checked && !customizeButton.parentDisabled,
    "Wait for the custom level to be selected and customize to be enabled"
  );

  info("Check the tracking protection toggle in the customize pane");
  let paneShown = waitForPaneChange("etpCustomize");
  synthesizeClick(customizeButton);
  await paneShown;
  let toggleControl = doc.getElementById(
    "setting-control-etpCustomTrackingProtectionEnabled"
  );
  if (!getMessageBar(toggleControl)) {
    await waitForMessageBar(toggleControl, true);
  }
  assertMessageBar(toggleControl);
  ok(
    getControl(doc, "etpCustomTrackingProtectionEnabled").disabled,
    "Tracking protection toggle disabled while controlled"
  );
  ok(
    getControl(doc, "etpCustomTrackingProtectionEnabledContext").parentDisabled,
    "Tracking protection context select disabled while controlled"
  );

  await maybeNavigateToPane("etp");

  info("Disable the extension from the message bar");
  let addon = await AddonManager.getAddonByID(EXTENSION_ID);
  let disableButton = messageBar.querySelector(
    "moz-button[data-l10n-id='disable-extension']"
  );
  ok(disableButton, "Message bar has a disable extension button");
  let messageBarHidden = waitForMessageBar(radioGroupControl, false);
  synthesizeClick(disableButton);
  await messageBarHidden;

  ok(!Services.prefs.getBoolPref(TP_PREF), "Tracking protection pref reset");
  ok(
    radioGroupControl.querySelector(".reenable-extensions-message-bar"),
    "Re-enable extension message bar shown"
  );
  ok(!standardRadio.disabled, "Standard radio enabled after disabling");
  ok(!strictRadio.disabled, "Strict radio enabled after disabling");
  ok(!customRadio.disabled, "Custom radio enabled after disabling");

  // Re-enable so the uninstall cleans up the ExtensionPreferencesManager state.
  await addon.enable();
  await extension.unload();

  gBrowser.removeCurrentTab();
});
