# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Regenerates the EXIF test images used by TestEXIFScanner.cpp.

The files this writes are committed, so this script only needs running when a
fixture changes. It exists so that the bytes in those files have a reviewable
provenance rather than arriving from nowhere.

Run from the top of the source tree:

    python3 image/test/gtest/generate_exif_fixtures.py

Every fixture is a real image. Rather than synthesizing a container, this takes
a JPEG and a PNG that a real encoder already produced and that are already in
the tree, and replaces or inserts only the metadata block. The result still
decodes, and only the part under test is authored here.

There is one exception, called out at its definition: no HEIF file exists in
the tree to borrow from, and since nothing locates EXIF inside one anyway, that
fixture only needs to be identifiable as HEIF.
"""

import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
TOPSRCDIR = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

# Real encoder output already in the tree, used as the carrier for the metadata
# blocks below.
JPEG_SOURCE = os.path.join(
    TOPSRCDIR, "layout", "reftests", "image", "image-exif-0-deg.jpg"
)
PNG_SOURCE = os.path.join(HERE, "green.png")
WEBP_SOURCE = os.path.join(HERE, "blend.webp")


class TIFFBuilder:
    """Builds a TIFF block bottom up.

    Every offset inside a TIFF block is relative to the start of the header, and
    an IFD has to know the offset of whatever it points at. Appending the target
    first and feeding back the offset it returns avoids writing placeholders and
    patching them later.
    """

    # EXIF type codes, from EXIF Section 4.6.2.
    BYTE = 1
    ASCII = 2
    SHORT = 3
    LONG = 4
    RATIONAL = 5
    UNDEFINED = 7

    def __init__(self, big_endian=False):
        self.e = ">" if big_endian else "<"
        self.data = bytearray()
        self.data += b"MM\x00\x2a" if big_endian else b"II\x2a\x00"
        # Patched by set_ifd0().
        self.data += struct.pack(self.e + "I", 0)

    def offset(self):
        return len(self.data)

    def add_rationals(self, pairs):
        at = self.offset()
        for numerator, denominator in pairs:
            self.data += struct.pack(self.e + "II", numerator, denominator)
        return at

    def add_bytes(self, raw):
        at = self.offset()
        self.data += raw
        return at

    def add_ifd(self, entries):
        """entries is a list of (tag, type, count, value_or_offset)."""
        at = self.offset()
        self.data += struct.pack(self.e + "H", len(entries))
        for tag, typ, count, value in entries:
            self.data += struct.pack(self.e + "HHI", tag, typ, count)
            # A value shorter than the four byte field is left aligned within
            # it, so a SHORT is two value bytes then two of padding. Packing it
            # as a 32 bit integer would put the padding first on big endian.
            if typ == self.SHORT and count == 1:
                self.data += struct.pack(self.e + "HH", value, 0)
            else:
                self.data += struct.pack(self.e + "I", value)
        self.data += struct.pack(self.e + "I", 0)  # No next IFD.
        return at

    def set_ifd0(self, at):
        struct.pack_into(self.e + "I", self.data, 4, at)

    def build(self):
        return bytes(self.data)


def exif_with_gps(
    latitude=((51, 1), (30, 1), (0, 1)),
    longitude=((0, 1), (7, 1), (39, 1)),
    big_endian=True,
):
    """A position, by default 51 30 N 0 7 39 W."""
    b = TIFFBuilder(big_endian)
    lat = b.add_rationals(latitude)
    lon = b.add_rationals(longitude)
    north = b.add_bytes(b"N\x00")
    west = b.add_bytes(b"W\x00")
    gps = b.add_ifd([
        (0x0000, b.BYTE, 4, 0x02030000),
        (0x0001, b.ASCII, 2, north),
        (0x0002, b.RATIONAL, 3, lat),
        (0x0003, b.ASCII, 2, west),
        (0x0004, b.RATIONAL, 3, lon),
    ])
    b.set_ifd0(
        b.add_ifd([
            (0x0112, b.SHORT, 1, 1),  # Orientation
            (0x8825, b.LONG, 1, gps),  # GPSInfoIFDPointer
        ])
    )
    return b.build()


def exif_without_gps(big_endian=True):
    b = TIFFBuilder(big_endian)
    exif = b.add_ifd([
        (0xA002, b.LONG, 1, 64),  # PixelXDimension
        (0xA003, b.LONG, 1, 48),  # PixelYDimension
    ])
    b.set_ifd0(
        b.add_ifd([
            (0x0112, b.SHORT, 1, 1),
            (0x8769, b.LONG, 1, exif),  # ExifIFDPointer
        ])
    )
    return b.build()


def exif_with_stripped_gps(big_endian=True):
    """A GPS IFD holding only GPSVersionID, as a partial strip leaves behind."""
    b = TIFFBuilder(big_endian)
    gps = b.add_ifd([(0x0000, b.BYTE, 4, 0x02030000)])
    b.set_ifd0(
        b.add_ifd([
            (0x0112, b.SHORT, 1, 1),
            (0x8825, b.LONG, 1, gps),
        ])
    )
    return b.build()


def exif_with_zeroed_gps(big_endian=True):
    """Coordinates left in place but blanked, as an in-place strip leaves."""
    return exif_with_gps(
        latitude=((0, 1), (0, 1), (0, 1)),
        longitude=((0, 1), (0, 1), (0, 1)),
        big_endian=big_endian,
    )


def exif_with_interoperability_ifd(big_endian=True):
    """The false positive case.

    The Interoperability IFD is reached from the Exif IFD via tag 0xA005 and is
    present in the large majority of camera JPEGs. Its tags 0x0001 and 0x0002
    are InteroperabilityIndex and InteroperabilityVersion, which collide with
    GPSLatitudeRef and GPSLatitude. A scanner that dispatched on tag number
    without tracking which IFD it was in would report a position here.
    """
    b = TIFFBuilder(big_endian)
    index = b.add_bytes(b"R98\x00")
    interop = b.add_ifd([
        (0x0001, b.ASCII, 4, index),
        (0x0002, b.UNDEFINED, 4, 0x30313030),
    ])
    exif = b.add_ifd([
        (0xA002, b.LONG, 1, 64),
        (0xA003, b.LONG, 1, 48),
        (0xA005, b.LONG, 1, interop),
    ])
    b.set_ifd0(
        b.add_ifd([
            (0x0112, b.SHORT, 1, 1),
            (0x8769, b.LONG, 1, exif),
        ])
    )
    return b.build()


def jpeg_with_exif(exif_block):
    """The source JPEG with its APP1 segment replaced by exif_block."""
    data = open(JPEG_SOURCE, "rb").read()
    assert data[:2] == b"\xff\xd8", "source is not a JPEG"

    out = bytearray(data[:2])
    payload = b"Exif\x00\x00" + exif_block
    out += b"\xff\xe1" + struct.pack(">H", len(payload) + 2) + payload

    # Copy the rest, dropping the APP1 the source already had so that the file
    # has exactly one.
    i = 2
    while i < len(data) - 1:
        if data[i] != 0xFF:
            break
        while i < len(data) and data[i] == 0xFF:
            i += 1
        marker = data[i]
        i += 1
        if marker == 0x01 or 0xD0 <= marker <= 0xD7:
            continue
        if marker in (0xDA, 0xD9):
            out += data[i - 2 :]
            return bytes(out)
        length = struct.unpack(">H", data[i : i + 2])[0]
        segment = data[i - 2 : i + length]
        if marker != 0xE1:
            out += segment
        i += length
    return bytes(out)


def png_with_exif(exif_block):
    """The source PNG with an eXIf chunk inserted before IDAT."""
    data = open(PNG_SOURCE, "rb").read()
    signature = data[:8]
    assert signature == b"\x89PNG\r\n\x1a\n", "source is not a PNG"

    def chunk(kind, payload):
        return (
            struct.pack(">I", len(payload))
            + kind
            + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
        )

    out = bytearray(signature)
    inserted = exif_block is None
    i = 8
    while i < len(data):
        length = struct.unpack(">I", data[i : i + 4])[0]
        kind = data[i + 4 : i + 8]
        whole = data[i : i + 12 + length]
        # The PNG specification requires eXIf to precede the image data.
        if kind == b"IDAT" and not inserted:
            out += chunk(b"eXIf", exif_block)
            inserted = True
        # Drop any eXIf the source already had.
        if kind != b"eXIf":
            out += whole
        i += 12 + length
    return bytes(out)


def minimal_heif():
    """Identifiable as HEIF, and nothing more.

    Unlike every other fixture here this is synthesized, because the tree has no
    HEIF file to borrow from. It is enough: nothing locates EXIF inside a HEIF,
    so the only thing this has to prove is that such a file is recognised and
    counted rather than being reported as having no location.
    """
    ftyp = b"\x00\x00\x00\x18" + b"ftyp" + b"heic" + b"\x00\x00\x00\x00" + b"heicmif1"
    return ftyp + b"\x00\x00\x00\x08" + b"free"


FIXTURES = {
    "exif_gps.jpg": lambda: jpeg_with_exif(exif_with_gps()),
    "exif_nogps.jpg": lambda: jpeg_with_exif(exif_without_gps()),
    "exif_gps_stripped.jpg": lambda: jpeg_with_exif(exif_with_stripped_gps()),
    "exif_gps_zeroed.jpg": lambda: jpeg_with_exif(exif_with_zeroed_gps()),
    "exif_interop.jpg": lambda: jpeg_with_exif(exif_with_interoperability_ifd()),
    # Little endian, so that the fixtures cover both byte orders.
    "exif_gps_le.jpg": lambda: jpeg_with_exif(exif_with_gps(big_endian=False)),
    "exif_gps.png": lambda: png_with_exif(exif_with_gps()),
    "exif_nogps.png": lambda: png_with_exif(None),
    "exif_gps.heic": minimal_heif,
}


def main():
    for name, build in sorted(FIXTURES.items()):
        path = os.path.join(HERE, name)
        data = build()
        with open(path, "wb") as f:
            f.write(data)
        print(f"wrote {name} ({len(data)} bytes)")

    # The WebP fixture is copied rather than built: all it has to do is be a
    # real WebP, so that one shows up as counted-but-not-scanned.
    webp = os.path.join(HERE, "exif_gps.webp")
    with open(WEBP_SOURCE, "rb") as src, open(webp, "wb") as dst:
        dst.write(src.read())
    print(f"copied exif_gps.webp from {os.path.relpath(WEBP_SOURCE, TOPSRCDIR)}")


if __name__ == "__main__":
    main()
