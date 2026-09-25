/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Document Zoom Management Code
 *
 * To use this, you'll need to have a global gBrowser variable
 * or pass the browser to be modified to the methods.
 */

var ZoomManager = {
  set useFullZoom(aVal) {
    Services.prefs.setBoolPref("browser.zoom.full", aVal);
  },

  useFullZoomForBrowser(aBrowser) {
    return this.useFullZoom || aBrowser.isSyntheticDocument;
  },

  getFullZoomForBrowser(aBrowser) {
    if (!this.useFullZoomForBrowser(aBrowser)) {
      return 1.0;
    }
    return this.getZoomForBrowser(aBrowser);
  },

  getZoomForBrowser(aBrowser) {
    let zoom = this.useFullZoomForBrowser(aBrowser)
      ? aBrowser.fullZoom
      : aBrowser.textZoom;
    // Round to remove any floating-point error.
    return Number(zoom ? zoom.toFixed(2) : 1);
  },

  setZoomForBrowser(aBrowser, aVal) {
    if (aVal < this.MIN || aVal > this.MAX) {
      throw Components.Exception("", Cr.NS_ERROR_INVALID_ARG);
    }

    if (this.useFullZoomForBrowser(aBrowser)) {
      aBrowser.textZoom = 1;
      aBrowser.fullZoom = aVal;
    } else {
      aBrowser.textZoom = aVal;
      aBrowser.fullZoom = 1;
    }
  },

  enlargeForBrowser(aBrowser) {
    var i =
      this.zoomValues.indexOf(this.snap(this.getZoomForBrowser(aBrowser))) + 1;
    if (i < this.zoomValues.length) {
      this.setZoomForBrowser(aBrowser, this.zoomValues[i]);
    }
  },

  reduceForBrowser(aBrowser) {
    var i =
      this.zoomValues.indexOf(this.snap(this.getZoomForBrowser(aBrowser))) - 1;
    if (i >= 0) {
      this.setZoomForBrowser(aBrowser, this.zoomValues[i]);
    }
  },

  enlarge(aBrowser = gBrowser) {
    this.enlargeForBrowser(aBrowser);
  },

  reduce(aBrowser = gBrowser) {
    this.reduceForBrowser(aBrowser);
  },

  reset(aBrowser = gBrowser) {
    this.setZoomForBrowser(aBrowser, 1);
  },

  toggleZoom(aBrowser = gBrowser) {
    var zoomLevel = this.getZoomForBrowser(aBrowser);

    this.useFullZoom = !this.useFullZoom;
    this.setZoomForBrowser(aBrowser, zoomLevel);
  },

  snap(aVal) {
    var values = this.zoomValues;
    for (var i = 0; i < values.length; i++) {
      if (values[i] >= aVal) {
        if (i > 0 && aVal - values[i - 1] < values[i] - aVal) {
          i--;
        }
        return values[i];
      }
    }
    return values[i - 1];
  },
};

{
  const { XPCOMUtils } = ChromeUtils.importESModule(
    "resource://gre/modules/XPCOMUtils.sys.mjs"
  );

  XPCOMUtils.defineLazyPreferenceGetter(
    ZoomManager,
    "MIN",
    "zoom.minPercent",
    30,
    null,
    v => v / 100
  );
  XPCOMUtils.defineLazyPreferenceGetter(
    ZoomManager,
    "MAX",
    "zoom.maxPercent",
    500,
    null,
    v => v / 100
  );

  XPCOMUtils.defineLazyPreferenceGetter(
    ZoomManager,
    "zoomValues",
    "toolkit.zoomManager.zoomValues",
    ".3,.5,.67,.8,.9,1,1.1,1.2,1.33,1.5,1.7,2,2.4,3,4,5",
    null,
    zoomValues => {
      zoomValues = zoomValues.split(",").map(parseFloat);
      zoomValues.sort((a, b) => a - b);

      while (zoomValues[0] < ZoomManager.MIN) {
        zoomValues.shift();
      }

      while (zoomValues[zoomValues.length - 1] > ZoomManager.MAX) {
        zoomValues.pop();
      }

      return zoomValues;
    }
  );

  XPCOMUtils.defineLazyPreferenceGetter(
    ZoomManager,
    "useFullZoom",
    "browser.zoom.full"
  );
}
