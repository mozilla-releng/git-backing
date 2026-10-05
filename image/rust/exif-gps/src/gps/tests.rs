// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

use exif::{Rational, SRational, Value};

use super::{classify, degrees, Parsed};
use crate::ffi::EXIFGPSPresence;

const BYTE: u16 = 1;
const ASCII: u16 = 2;
const SHORT: u16 = 3;
const LONG: u16 = 4;
const RATIONAL: u16 = 5;
const UNDEFINED: u16 = 7;
const SRATIONAL: u16 = 10;

const ORIENTATION: u16 = 0x0112;
const EXIF_IFD_POINTER: u16 = 0x8769;
const GPS_IFD_POINTER: u16 = 0x8825;
const INTEROP_IFD_POINTER: u16 = 0xA005;

const GPS_VERSION: u16 = 0x0000;
const GPS_LAT_REF: u16 = 0x0001;
const GPS_LAT: u16 = 0x0002;
const GPS_LON_REF: u16 = 0x0003;
const GPS_LON: u16 = 0x0004;
const GPS_DEST_LAT: u16 = 0x0014;
const GPS_DEST_LON: u16 = 0x0016;

/// The four bytes an entry carries, resolved once the layout is known.
enum Val {
    /// A SHORT, left-aligned with the trailing two bytes as padding.
    Short(u16),
    /// Four bytes written verbatim, for types with no byte order of their own.
    Bytes([u8; 4]),
    /// A pointer to the IFD with this index.
    Ifd(usize),
    /// A pointer to the out-of-line value with this index.
    Blob(usize),
    /// A LONG written as given, for offsets that are meant to be wrong.
    Raw(u32),
}

/// An out-of-line value, too big for an entry's four bytes.
enum Blob {
    Rationals(Vec<(u32, u32)>),
    SRationals(Vec<(i32, i32)>),
}

struct Entry {
    tag: u16,
    typ: u16,
    count: u32,
    val: Val,
}

fn entry(tag: u16, typ: u16, count: u32, val: Val) -> Entry {
    Entry {
        tag,
        typ,
        count,
        val,
    }
}

/// Builds a TIFF block from a list of IFDs and their out-of-line values.
///
/// Laid out as header, then the IFDs in order, then the values, so that every
/// offset is known before anything is written. IFD 0 is the root.
struct Tiff {
    little_endian: bool,
    ifds: Vec<Vec<Entry>>,
    blobs: Vec<Blob>,
}

impl Tiff {
    fn new(little_endian: bool) -> Self {
        Self {
            little_endian,
            ifds: Vec::new(),
            blobs: Vec::new(),
        }
    }

    fn ifd(mut self, entries: Vec<Entry>) -> Self {
        self.ifds.push(entries);
        self
    }

    fn blob(mut self, blob: Blob) -> Self {
        self.blobs.push(blob);
        self
    }

    fn u16(&self, v: u16) -> [u8; 2] {
        if self.little_endian {
            v.to_le_bytes()
        } else {
            v.to_be_bytes()
        }
    }

    fn u32(&self, v: u32) -> [u8; 4] {
        if self.little_endian {
            v.to_le_bytes()
        } else {
            v.to_be_bytes()
        }
    }

    fn i32(&self, v: i32) -> [u8; 4] {
        if self.little_endian {
            v.to_le_bytes()
        } else {
            v.to_be_bytes()
        }
    }

    fn blob_bytes(&self, blob: &Blob) -> Vec<u8> {
        let mut out = Vec::new();
        match blob {
            Blob::Rationals(v) => {
                for (num, denom) in v {
                    out.extend_from_slice(&self.u32(*num));
                    out.extend_from_slice(&self.u32(*denom));
                }
            }
            Blob::SRationals(v) => {
                for (num, denom) in v {
                    out.extend_from_slice(&self.i32(*num));
                    out.extend_from_slice(&self.i32(*denom));
                }
            }
        }
        out
    }

    fn build(&self) -> Vec<u8> {
        // Where each IFD and each value will land.
        let mut ifd_offsets = Vec::new();
        let mut next = 8u32;
        for entries in &self.ifds {
            ifd_offsets.push(next);
            next += 2 + 12 * entries.len() as u32 + 4;
        }
        let mut blob_offsets = Vec::new();
        for blob in &self.blobs {
            blob_offsets.push(next);
            next += self.blob_bytes(blob).len() as u32;
        }

        let mut out = Vec::new();
        out.extend_from_slice(if self.little_endian {
            b"II\x2A\x00"
        } else {
            b"MM\x00\x2A"
        });
        out.extend_from_slice(&self.u32(ifd_offsets.first().copied().unwrap_or(0)));

        for entries in &self.ifds {
            out.extend_from_slice(&self.u16(entries.len() as u16));
            for e in entries {
                out.extend_from_slice(&self.u16(e.tag));
                out.extend_from_slice(&self.u16(e.typ));
                out.extend_from_slice(&self.u32(e.count));
                match &e.val {
                    Val::Short(v) => {
                        out.extend_from_slice(&self.u16(*v));
                        out.extend_from_slice(&[0, 0]);
                    }
                    Val::Bytes(b) => out.extend_from_slice(b),
                    Val::Ifd(i) => out.extend_from_slice(&self.u32(ifd_offsets[*i])),
                    Val::Blob(i) => out.extend_from_slice(&self.u32(blob_offsets[*i])),
                    Val::Raw(v) => out.extend_from_slice(&self.u32(*v)),
                }
            }
            out.extend_from_slice(&self.u32(0)); // no next IFD
        }

        for blob in &self.blobs {
            out.extend_from_slice(&self.blob_bytes(blob));
        }
        out
    }
}

/// 51 deg 30 min 0 sec.
fn latitude() -> Blob {
    Blob::Rationals(vec![(51, 1), (30, 1), (0, 1)])
}

/// 0 deg 7 min 39 sec.
fn longitude() -> Blob {
    Blob::Rationals(vec![(0, 1), (7, 1), (39, 1)])
}

/// A block whose GPS IFD carries `entries`, reached from IFD 0.
fn with_gps_ifd(little_endian: bool, entries: Vec<Entry>, blobs: Vec<Blob>) -> Vec<u8> {
    let mut t = Tiff::new(little_endian)
        .ifd(vec![
            entry(ORIENTATION, SHORT, 1, Val::Short(1)),
            entry(GPS_IFD_POINTER, LONG, 1, Val::Ifd(1)),
        ])
        .ifd(entries);
    for blob in blobs {
        t = t.blob(blob);
    }
    t.build()
}

/// A block carrying a real position.
fn with_position(little_endian: bool) -> Vec<u8> {
    with_gps_ifd(
        little_endian,
        vec![
            entry(GPS_LAT, RATIONAL, 3, Val::Blob(0)),
            entry(GPS_LON, RATIONAL, 3, Val::Blob(1)),
        ],
        vec![latitude(), longitude()],
    )
}

fn presence_of(block: &[u8]) -> EXIFGPSPresence {
    match classify(block) {
        Parsed::Walked(p) => p,
        Parsed::Unparseable => panic!("expected the block to parse"),
    }
}

fn is_unparseable(block: &[u8]) -> bool {
    matches!(classify(block), Parsed::Unparseable)
}

// ------------------------------------------------------- decimal degrees

#[test]
fn a_coordinate_becomes_decimal_degrees() {
    let lat = Value::Rational(vec![
        Rational { num: 51, denom: 1 },
        Rational { num: 30, denom: 1 },
        Rational { num: 0, denom: 1 },
    ]);
    assert_eq!(degrees(&lat, 90), Some(51.5));

    let lon = Value::Rational(vec![
        Rational { num: 0, denom: 1 },
        Rational { num: 7, denom: 1 },
        Rational { num: 39, denom: 1 },
    ]);
    assert_eq!(degrees(&lon, 180), Some(0.1275));
}

#[test]
fn a_fraction_may_live_in_the_minutes_or_the_seconds() {
    // Encoders routinely write 30.5 minutes as 305/10, or 39 seconds as
    // 3900/100. Comparing components as integers would reject both.
    let minutes = Value::Rational(vec![
        Rational { num: 51, denom: 1 },
        Rational {
            num: 305,
            denom: 10,
        },
        Rational { num: 0, denom: 1 },
    ]);
    assert_eq!(degrees(&minutes, 90), Some(51.508_333_333_333_33));

    let seconds = Value::Rational(vec![
        Rational { num: 0, denom: 1 },
        Rational { num: 7, denom: 1 },
        Rational {
            num: 3900,
            denom: 100,
        },
    ]);
    assert_eq!(degrees(&seconds, 180), Some(0.1275));
}

#[test]
fn a_zero_denominator_is_not_a_coordinate() {
    // Rational::to_f64 divides unchecked, so without an explicit check this
    // would be an infinity, and 0/0 would be a NaN that compares false against
    // every test including the zero-position one.
    let value = Value::Rational(vec![
        Rational { num: 51, denom: 0 },
        Rational { num: 30, denom: 1 },
        Rational { num: 0, denom: 1 },
    ]);
    assert_eq!(degrees(&value, 90), None);

    let nan = Value::Rational(vec![
        Rational { num: 0, denom: 0 },
        Rational { num: 0, denom: 1 },
        Rational { num: 0, denom: 1 },
    ]);
    assert_eq!(degrees(&nan, 90), None);
}

#[test]
fn every_component_has_to_be_in_range() {
    // Three well-formed rationals are not on their own a position: an entry
    // whose value offset is wrong lands on arbitrary bytes, and those divide
    // into plausible-looking numbers.
    let too_many_degrees = Value::Rational(vec![
        Rational { num: 91, denom: 1 },
        Rational { num: 0, denom: 1 },
        Rational { num: 0, denom: 1 },
    ]);
    assert_eq!(degrees(&too_many_degrees, 90), None);
    // The same value is a legal longitude.
    assert_eq!(degrees(&too_many_degrees, 180), Some(91.0));

    for component in 1..3 {
        let mut parts = vec![Rational { num: 0, denom: 1 }; 3];
        parts[component] = Rational { num: 60, denom: 1 };
        let value = Value::Rational(parts);
        assert_eq!(degrees(&value, 90), None, "component {component}");
    }

    // The bounds themselves are allowed: 90 degrees exactly is the pole, and
    // 59.999 minutes is not yet a whole degree.
    let pole = Value::Rational(vec![
        Rational { num: 90, denom: 1 },
        Rational { num: 0, denom: 1 },
        Rational { num: 0, denom: 1 },
    ]);
    assert_eq!(degrees(&pole, 90), Some(90.0));
}

#[test]
fn only_three_rationals_are_a_coordinate() {
    for count in [0usize, 1, 2, 4] {
        let value = Value::Rational(vec![Rational { num: 1, denom: 1 }; count]);
        assert_eq!(degrees(&value, 90), None, "{count} components");
    }
    // A type that is not a rational at all.
    assert_eq!(degrees(&Value::Long(vec![51, 30, 0]), 90), None);
    assert_eq!(degrees(&Value::Short(vec![51, 30, 0]), 90), None);
}

#[test]
fn signed_rationals_parse_as_their_magnitude() {
    // AOSP's camera stack writes SRATIONAL here, which the specification does
    // not allow. The sign of a component is meaningless for a coordinate whose
    // sign lives in a separate tag, so take the magnitude.
    let signed = Value::SRational(vec![
        SRational { num: -51, denom: 1 },
        SRational { num: 30, denom: -1 },
        SRational { num: 0, denom: 1 },
    ]);
    assert_eq!(degrees(&signed, 90), Some(51.5));

    // Taking the magnitude must not let an out-of-range value through.
    let out_of_range = Value::SRational(vec![
        SRational {
            num: -500,
            denom: 1,
        },
        SRational { num: 0, denom: 1 },
        SRational { num: 0, denom: 1 },
    ]);
    assert_eq!(degrees(&out_of_range, 90), None);
}

// ------------------------------------------------------------- presence

#[test]
fn a_position_is_found_in_both_byte_orders() {
    for little_endian in [true, false] {
        assert_eq!(
            presence_of(&with_position(little_endian)),
            EXIFGPSPresence::Position,
            "little_endian={little_endian}"
        );
    }
}

#[test]
fn no_gps_ifd_at_all() {
    let block = Tiff::new(true)
        .ifd(vec![entry(ORIENTATION, SHORT, 1, Val::Short(1))])
        .build();
    assert_eq!(presence_of(&block), EXIFGPSPresence::None);
}

#[test]
fn a_gps_ifd_with_nothing_usable_in_it() {
    // A version and nothing else is what a partially stripped file looks like.
    let block = with_gps_ifd(
        true,
        vec![entry(GPS_VERSION, BYTE, 4, Val::Bytes([2, 3, 0, 0]))],
        vec![],
    );
    assert_eq!(presence_of(&block), EXIFGPSPresence::NoPosition);
}

#[test]
fn a_latitude_without_a_longitude_is_not_a_position() {
    let block = with_gps_ifd(
        true,
        vec![entry(GPS_LAT, RATIONAL, 3, Val::Blob(0))],
        vec![latitude()],
    );
    assert_eq!(presence_of(&block), EXIFGPSPresence::NoPosition);
}

#[test]
fn both_coordinates_exactly_zero_is_null_island() {
    let block = with_gps_ifd(
        true,
        vec![
            entry(GPS_LAT, RATIONAL, 3, Val::Blob(0)),
            entry(GPS_LON, RATIONAL, 3, Val::Blob(1)),
        ],
        vec![
            Blob::Rationals(vec![(0, 1), (0, 1), (0, 1)]),
            Blob::Rationals(vec![(0, 1), (0, 1), (0, 1)]),
        ],
    );
    assert_eq!(presence_of(&block), EXIFGPSPresence::ZeroPosition);
}

#[test]
fn a_zero_latitude_with_a_real_longitude_is_a_position() {
    // The equator is a place. Only both being zero is the blanked coordinate.
    let block = with_gps_ifd(
        true,
        vec![
            entry(GPS_LAT, RATIONAL, 3, Val::Blob(0)),
            entry(GPS_LON, RATIONAL, 3, Val::Blob(1)),
        ],
        vec![
            Blob::Rationals(vec![(0, 1), (0, 1), (0, 1)]),
            Blob::Rationals(vec![(78, 1), (30, 1), (0, 1)]),
        ],
    );
    assert_eq!(presence_of(&block), EXIFGPSPresence::Position);
}

#[test]
fn destination_coordinates_are_not_a_position() {
    // GPSDestLatitude and GPSDestLongitude say where the camera was pointed,
    // not where it stood.
    let block = with_gps_ifd(
        true,
        vec![
            entry(GPS_DEST_LAT, RATIONAL, 3, Val::Blob(0)),
            entry(GPS_DEST_LON, RATIONAL, 3, Val::Blob(1)),
        ],
        vec![latitude(), longitude()],
    );
    assert_eq!(presence_of(&block), EXIFGPSPresence::NoPosition);
}

#[test]
fn the_reference_tags_do_not_change_the_classification() {
    // The hemisphere is not read: a position is a position either side of the
    // equator, and the sign never leaves this crate.
    for (lat_ref, lon_ref) in [(b'N', b'E'), (b'S', b'W')] {
        let block = with_gps_ifd(
            true,
            vec![
                entry(GPS_LAT_REF, ASCII, 2, Val::Bytes([lat_ref, 0, 0, 0])),
                entry(GPS_LAT, RATIONAL, 3, Val::Blob(0)),
                entry(GPS_LON_REF, ASCII, 2, Val::Bytes([lon_ref, 0, 0, 0])),
                entry(GPS_LON, RATIONAL, 3, Val::Blob(1)),
            ],
            vec![latitude(), longitude()],
        );
        assert_eq!(
            presence_of(&block),
            EXIFGPSPresence::Position,
            "{lat_ref} {lon_ref}"
        );
    }
}

#[test]
fn signed_coordinates_are_a_position() {
    let block = with_gps_ifd(
        true,
        vec![
            entry(GPS_LAT, SRATIONAL, 3, Val::Blob(0)),
            entry(GPS_LON, SRATIONAL, 3, Val::Blob(1)),
        ],
        vec![
            Blob::SRationals(vec![(-51, 1), (30, 1), (0, 1)]),
            Blob::SRationals(vec![(0, 1), (7, 1), (39, 1)]),
        ],
    );
    assert_eq!(presence_of(&block), EXIFGPSPresence::Position);
}

#[test]
fn the_interoperability_ifd_is_not_mistaken_for_gps() {
    // 0x0001 and 0x0002 are GPSLatitudeRef and GPSLatitude in the GPS IFD, but
    // InteroperabilityIndex and InteroperabilityVersion in the Interoperability
    // IFD, which the large majority of camera JPEGs carry. Dispatching on tag
    // number alone would report a position on almost every photo.
    let block = Tiff::new(true)
        .ifd(vec![
            entry(ORIENTATION, SHORT, 1, Val::Short(1)),
            entry(EXIF_IFD_POINTER, LONG, 1, Val::Ifd(1)),
        ])
        .ifd(vec![entry(INTEROP_IFD_POINTER, LONG, 1, Val::Ifd(2))])
        .ifd(vec![
            entry(0x0001, ASCII, 4, Val::Bytes(*b"R98\0")),
            entry(0x0002, UNDEFINED, 4, Val::Bytes([0x30, 0x31, 0x30, 0x30])),
        ])
        .build();
    assert_eq!(presence_of(&block), EXIFGPSPresence::None);
}

// ------------------------------------------------------ hostile structure

#[test]
fn a_coordinate_offset_out_of_range_is_not_a_position() {
    for offset in [0x7FFF_FFFFu32, 0xFFFF_FFFF, 0] {
        let block = with_gps_ifd(
            true,
            vec![
                entry(GPS_LAT, RATIONAL, 3, Val::Raw(offset)),
                entry(GPS_LON, RATIONAL, 3, Val::Blob(0)),
            ],
            vec![longitude()],
        );
        assert_ne!(
            classify_presence_or_none(&block),
            EXIFGPSPresence::Position,
            "offset {offset:#x}"
        );
    }
}

#[test]
fn an_overflowing_component_count_is_not_a_position() {
    let block = with_gps_ifd(
        true,
        vec![
            entry(GPS_LAT, RATIONAL, 0xFFFF_FFFF, Val::Blob(0)),
            entry(GPS_LON, RATIONAL, 3, Val::Blob(1)),
        ],
        vec![latitude(), longitude()],
    );
    assert_ne!(classify_presence_or_none(&block), EXIFGPSPresence::Position);
}

#[test]
fn a_gps_pointer_out_of_range_finds_nothing() {
    for offset in [0x7FFF_FFFFu32, 0xFFFF_FFFF] {
        let block = Tiff::new(true)
            .ifd(vec![entry(GPS_IFD_POINTER, LONG, 1, Val::Raw(offset))])
            .build();
        assert_ne!(
            classify_presence_or_none(&block),
            EXIFGPSPresence::Position,
            "offset {offset:#x}"
        );
    }
}

#[test]
fn a_pointer_cycle_terminates() {
    // IFD 0 begins at offset 8, so this points at itself.
    let block = Tiff::new(true)
        .ifd(vec![entry(EXIF_IFD_POINTER, LONG, 1, Val::Raw(8))])
        .build();
    assert_ne!(classify_presence_or_none(&block), EXIFGPSPresence::Position);

    let gps_cycle = Tiff::new(true)
        .ifd(vec![entry(GPS_IFD_POINTER, LONG, 1, Val::Raw(8))])
        .build();
    assert_ne!(
        classify_presence_or_none(&gps_cycle),
        EXIFGPSPresence::Position
    );
}

#[test]
fn a_truncation_sweep_reaches_a_verdict_at_every_length() {
    let full = with_position(true);
    for n in 0..=full.len() {
        // Nothing may panic, and a position must never be invented from fewer
        // bytes than the block needs.
        if let Parsed::Walked(presence) = classify(&full[..n]) {
            if n < full.len() {
                assert_ne!(presence, EXIFGPSPresence::Position, "prefix of {n}");
            }
        }
    }
    assert_eq!(presence_of(&full), EXIFGPSPresence::Position);
}

#[test]
fn garbage_and_emptiness_are_unparseable() {
    assert!(is_unparseable(b""));
    assert!(is_unparseable(b"not an exif block"));
    // A header with nothing behind it.
    assert!(is_unparseable(b"II\x2A\x00\x08\x00\x00\x00"));
}

/// The presence a block reports, treating an unparseable block as having said
/// nothing. Used where the point is that no position is found, and whether the
/// crate rejects the block outright or walks it and finds nothing is beside it.
fn classify_presence_or_none(block: &[u8]) -> EXIFGPSPresence {
    match classify(block) {
        Parsed::Walked(p) => p,
        Parsed::Unparseable => EXIFGPSPresence::None,
    }
}
