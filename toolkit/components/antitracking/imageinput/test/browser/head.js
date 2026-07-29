/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Set here rather than only in browser.toml so that flipping the default off
// cannot quietly turn these into tests that assert nothing. head.js is
// evaluated in every file's scope, so this runs first for all of them.
add_setup(async function enableImageInputTelemetry() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.imageInputTelemetry.enabled", true],
      ["privacy.imageInputTelemetry.enableTestMode", true],
    ],
  });
});

// Image files are built here rather than checked in as binaries. Nothing in
// this feature decodes an image -- it walks the container to find the EXIF
// block and stops -- so a file only has to be structurally right up to that
// point, and a few dozen bytes of DataView is easier to review than a blob.

/**
 * A bare little-endian TIFF block whose GPS IFD holds a real position:
 * 51 degrees 30 minutes north, 0 degrees 7 minutes 39 seconds west.
 *
 * Offsets are relative to the start of the TIFF header. IFD0 sits at 8, its
 * GPS pointer names 26, and the two coordinate entries point at rationals
 * beginning at 56 and 80. Those numbers are the byte layout below; changing an
 * entry means recomputing them.
 */
function tiffBlockWithPosition() {
  const bytes = [];
  const u8 = v => bytes.push(v & 0xff);
  const u16 = v => {
    u8(v);
    u8(v >> 8);
  };
  const u32 = v => {
    u8(v);
    u8(v >> 8);
    u8(v >> 16);
    u8(v >> 24);
  };

  u8(0x49); // "II", little endian
  u8(0x49);
  u16(42);
  u32(8); // IFD0 offset

  // IFD0: one entry, the GPS IFD pointer.
  u16(1);
  u16(0x8825); // GPSInfoIFDPointer
  u16(4); // LONG
  u32(1);
  u32(26);
  u32(0); // no next IFD

  // GPS IFD.
  u16(2);
  u16(0x0002); // GPSLatitude
  u16(5); // RATIONAL
  u32(3);
  u32(56);
  u16(0x0004); // GPSLongitude
  u16(5);
  u32(3);
  u32(80);
  u32(0);

  for (const numerator of [51, 30, 0]) {
    u32(numerator);
    u32(1);
  }
  for (const numerator of [0, 7, 39]) {
    u32(numerator);
    u32(1);
  }

  return bytes;
}

/**
 * A GPS IFD holding nothing but GPSVersionID, which is what a partially
 * stripped file looks like: the tag survived, the position did not.
 */
function tiffBlockWithStrippedPosition() {
  const bytes = [];
  const u8 = v => bytes.push(v & 0xff);
  const u16 = v => {
    u8(v);
    u8(v >> 8);
  };
  const u32 = v => {
    u8(v);
    u8(v >> 8);
    u8(v >> 16);
    u8(v >> 24);
  };

  u8(0x49);
  u8(0x49);
  u16(42);
  u32(8);

  u16(1);
  u16(0x8825);
  u16(4);
  u32(1);
  u32(26);
  u32(0);

  u16(1);
  u16(0x0000); // GPSVersionID
  u16(1); // BYTE
  u32(4);
  u32(0x00000302);
  u32(0);
  return bytes;
}

function asciiBytes(text) {
  return Array.from(text, character => character.charCodeAt(0));
}

/**
 * A JPEG carrying the given TIFF block in an EXIF APP1 segment, or none at all
 * when aTIFFBlock is null.
 */
function makeJPEG(aTIFFBlock) {
  const bytes = [0xff, 0xd8]; // SOI

  if (aTIFFBlock) {
    const payload = [...asciiBytes("Exif\0\0"), ...aTIFFBlock];
    const length = payload.length + 2;
    bytes.push(0xff, 0xe1, (length >> 8) & 0xff, length & 0xff, ...payload);
  }

  bytes.push(0xff, 0xda, 0x00, 0x02); // SOS
  bytes.push(0xff, 0xd9); // EOI
  return new Uint8Array(bytes);
}

/** A PNG carrying the given TIFF block in an eXIf chunk. */
function makePNG(aTIFFBlock) {
  const bytes = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];

  const chunk = (type, data) => {
    const length = data.length;
    bytes.push(
      (length >>> 24) & 0xff,
      (length >>> 16) & 0xff,
      (length >>> 8) & 0xff,
      length & 0xff
    );
    bytes.push(...asciiBytes(type));
    bytes.push(...data);
    // CRCs are not verified by the scanner, which never decodes the image.
    bytes.push(0, 0, 0, 0);
  };

  chunk("IHDR", new Array(13).fill(0));
  if (aTIFFBlock) {
    chunk("eXIf", aTIFFBlock);
  }
  chunk("IEND", []);
  return new Uint8Array(bytes);
}

/**
 * A file whose leading bytes say HEIF. Nothing locates EXIF inside one, so this
 * is here to prove such a file is still counted and still reports its real
 * format.
 */
function makeHEIC() {
  const bytes = [0x00, 0x00, 0x00, 0x18];
  bytes.push(...asciiBytes("ftypheic"));
  bytes.push(...new Array(32).fill(0));
  return new Uint8Array(bytes);
}

/**
 * The scan is asynchronous by design, so flushing child data is not on its own
 * a synchronisation point: the event may not exist yet when the flush happens.
 */
async function waitForImageInputEvents(aCount) {
  await TestUtils.waitForCondition(async () => {
    await Services.fog.testFlushAllChildren();
    const events = Glean.imageInput.offered.testGetValue();
    return events && events.length >= aCount;
  }, `waiting for ${aCount} image_input.offered event(s)`);

  await Services.fog.testFlushAllChildren();
  return Glean.imageInput.offered.testGetValue();
}

/**
 * Asserts that an action records nothing.
 *
 * Waiting for a timeout to expire would be both slow and unreliable, so instead
 * the action under test is followed by one known to record, and the assertion
 * is that only the second one shows up. If the first ever starts recording,
 * this fails deterministically rather than intermittently.
 *
 * aControlAction defaults to a drop. Pass one matching the path under test, so
 * that a regression somewhere else cannot fail this test instead.
 */
async function assertNoEventsFrom(
  aBrowser,
  aNegativeAction,
  aControlAction = recordKnownGoodEvent
) {
  await Services.fog.testResetFOG();
  await aNegativeAction();

  await aControlAction(aBrowser);

  const events = await waitForImageInputEvents(1);
  Assert.equal(
    events.length,
    1,
    "only the known-good action recorded an event"
  );
  return events[0];
}

/** Drops a geotagged JPEG on the page, which always records exactly one event. */
async function recordKnownGoodEvent(aBrowser) {
  await dropFilesOnPage(aBrowser, [
    {
      name: "known-good.jpg",
      type: "image/jpeg",
      bytes: makeJPEG(tiffBlockWithPosition()),
    },
  ]);
}

/**
 * Synthesizes a drop of the given files onto the page body.
 *
 * privacy.imageInputTelemetry.enableTestMode is what lets this count: a drop
 * built from script is an untrusted event, and the production path deliberately
 * ignores those.
 */
async function dropFilesOnPage(aBrowser, aFiles) {
  await SpecialPowers.spawn(aBrowser, [aFiles], async files => {
    const transfer = new content.DataTransfer();
    for (const file of files) {
      transfer.items.add(
        new content.File([file.bytes], file.name, { type: file.type })
      );
    }

    const event = content.document.createEvent("DragEvent");
    event.initDragEvent(
      "drop",
      true,
      true,
      content.window,
      0,
      0,
      0,
      0,
      0,
      false,
      false,
      false,
      false,
      0,
      content.document.body,
      transfer
    );

    const seen = new Promise(resolve => {
      content.document.addEventListener(
        "drop",
        e => {
          // Touching dataTransfer is what hands the files over, and so what
          // the probe hooks.
          void e.dataTransfer;
          resolve();
        },
        { once: true }
      );
    });

    content.document.dispatchEvent(event);
    await seen;
  });
}
