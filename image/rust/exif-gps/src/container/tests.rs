// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

use super::{scan, EXIFContainer, EXIFGPSPresence, EXIFScanOutcome};

/// Scans bytes that are the whole file.
fn scan_all(bytes: &[u8]) -> (EXIFContainer, EXIFScanOutcome) {
    let r = scan(bytes, bytes.len() as u64);
    (r.container, r.outcome)
}

/// Scans the first `n` bytes of a longer file, telling the scanner the real
/// size so it can tell a short read from a short file.
fn scan_prefix(bytes: &[u8], n: usize) -> (EXIFContainer, EXIFScanOutcome) {
    let r = scan(&bytes[..n.min(bytes.len())], bytes.len() as u64);
    (r.container, r.outcome)
}

/// What a whole file says about where it was taken.
fn gps_of(bytes: &[u8]) -> EXIFGPSPresence {
    scan(bytes, bytes.len() as u64).gps_presence
}

fn push_u16_le(out: &mut Vec<u8>, v: u16) {
    out.extend_from_slice(&v.to_le_bytes());
}

fn push_u32_le(out: &mut Vec<u8>, v: u32) {
    out.extend_from_slice(&v.to_le_bytes());
}

/// A bare little-endian TIFF block whose IFD0 points at a GPS attribute IFD
/// carrying a real latitude and longitude. The offsets are laid out by hand so
/// that the structure stays visible here rather than being computed.
///
/// 0 header, 8 IFD0, 26 GPS IFD, 56 latitude rationals, 80 longitude.
pub fn tiff_block_with_position() -> Vec<u8> {
    let mut b = Vec::new();
    b.extend_from_slice(b"II\x2A\x00");
    push_u32_le(&mut b, 8);

    // IFD0: one entry, the GPS IFD pointer.
    push_u16_le(&mut b, 1);
    push_u16_le(&mut b, 0x8825); // GPSInfoIFDPointer
    push_u16_le(&mut b, 4); // LONG
    push_u32_le(&mut b, 1);
    push_u32_le(&mut b, 26);
    push_u32_le(&mut b, 0); // no next IFD

    // GPS IFD at 26: latitude and longitude, values stored out of line.
    push_u16_le(&mut b, 2);
    push_u16_le(&mut b, 0x0002); // GPSLatitude
    push_u16_le(&mut b, 5); // RATIONAL
    push_u32_le(&mut b, 3);
    push_u32_le(&mut b, 56);
    push_u16_le(&mut b, 0x0004); // GPSLongitude
    push_u16_le(&mut b, 5);
    push_u32_le(&mut b, 3);
    push_u32_le(&mut b, 80);
    push_u32_le(&mut b, 0);

    // 51 deg 30 min 0 sec, then 0 deg 7 min 39 sec.
    for (num, denom) in [(51u32, 1u32), (30, 1), (0, 1), (0, 1), (7, 1), (39, 1)] {
        push_u32_le(&mut b, num);
        push_u32_le(&mut b, denom);
    }

    assert_eq!(
        b.len(),
        104,
        "layout changed; the hard-coded offsets are stale"
    );
    b
}

/// A bare little-endian TIFF block with metadata but no GPS IFD.
pub fn tiff_block_without_position() -> Vec<u8> {
    let mut b = Vec::new();
    b.extend_from_slice(b"II\x2A\x00");
    push_u32_le(&mut b, 8);
    push_u16_le(&mut b, 1);
    push_u16_le(&mut b, 0x0112); // Orientation
    push_u16_le(&mut b, 3); // SHORT
    push_u32_le(&mut b, 1);
    // A value shorter than four bytes is left-aligned in the value field.
    push_u16_le(&mut b, 1);
    push_u16_le(&mut b, 0);
    push_u32_le(&mut b, 0);
    b
}

pub struct Jpeg(Vec<u8>);

impl Jpeg {
    pub fn new() -> Self {
        Self(vec![0xFF, 0xD8])
    }

    /// A marker segment, whose length field counts its own two bytes.
    fn segment(mut self, marker: u8, payload: &[u8]) -> Self {
        self.0.push(0xFF);
        self.0.push(marker);
        self.0
            .extend_from_slice(&((payload.len() + 2) as u16).to_be_bytes());
        self.0.extend_from_slice(payload);
        self
    }

    pub fn exif_app1(self, block: &[u8]) -> Self {
        let mut payload = b"Exif\0\0".to_vec();
        payload.extend_from_slice(block);
        self.segment(0xE1, &payload)
    }

    /// An Exif APP1 padded out so that the segment runs past a prefix under
    /// test, which is how a clipped block is produced.
    fn padded_exif_app1(self, block: &[u8], payload_len: usize) -> Self {
        let mut payload = b"Exif\0\0".to_vec();
        payload.extend_from_slice(block);
        payload.resize(payload_len, 0);
        self.segment(0xE1, &payload)
    }

    fn xmp_app1(self) -> Self {
        let mut payload = b"http://ns.adobe.com/xap/1.0/\0".to_vec();
        payload.extend_from_slice(b"<x:xmpmeta/>");
        self.segment(0xE1, &payload)
    }

    pub fn jfif_app0(self) -> Self {
        self.segment(0xE0, b"JFIF\0\x01\x02\x00\x00\x01\x00\x01\x00\x00")
    }

    fn raw(mut self, bytes: &[u8]) -> Self {
        self.0.extend_from_slice(bytes);
        self
    }

    pub fn sos(mut self) -> Self {
        self.0.extend_from_slice(&[0xFF, 0xDA, 0x00, 0x02]);
        self
    }

    pub fn build(self) -> Vec<u8> {
        self.0
    }
}

pub struct Png(Vec<u8>);

impl Png {
    pub fn new() -> Self {
        Self(b"\x89PNG\r\n\x1A\n".to_vec())
    }

    /// A chunk with a zero placeholder CRC: the scanner must not verify CRCs,
    /// because a bad one would not move the next chunk.
    pub fn chunk(mut self, ctype: [u8; 4], data: &[u8]) -> Self {
        self.0.extend_from_slice(&(data.len() as u32).to_be_bytes());
        self.0.extend_from_slice(&ctype);
        self.0.extend_from_slice(data);
        self.0.extend_from_slice(&[0, 0, 0, 0]);
        self
    }

    /// A chunk header whose declared length is a lie, modelling a file whose
    /// chunk overruns its own end.
    fn lying_chunk(mut self, ctype: [u8; 4], declared: u32, data: &[u8]) -> Self {
        self.0.extend_from_slice(&declared.to_be_bytes());
        self.0.extend_from_slice(&ctype);
        self.0.extend_from_slice(data);
        self
    }

    pub fn ihdr(self) -> Self {
        self.chunk(*b"IHDR", &[0u8; 13])
    }

    pub fn idat(self, len: usize) -> Self {
        self.chunk(*b"IDAT", &vec![0u8; len])
    }

    pub fn iend(self) -> Self {
        self.chunk(*b"IEND", &[])
    }

    pub fn build(self) -> Vec<u8> {
        self.0
    }
}

pub struct WebP(Vec<u8>);

impl WebP {
    pub fn new() -> Self {
        Self(Vec::new())
    }

    /// A RIFF chunk. The payload is padded to an even length, and the padding
    /// byte is not counted by the size field.
    pub fn chunk(mut self, fourcc: [u8; 4], data: &[u8]) -> Self {
        self.0.extend_from_slice(&fourcc);
        self.0.extend_from_slice(&(data.len() as u32).to_le_bytes());
        self.0.extend_from_slice(data);
        if data.len() % 2 == 1 {
            self.0.push(0);
        }
        self
    }

    pub fn build(self) -> Vec<u8> {
        let mut out = b"RIFF".to_vec();
        // The size counts everything after itself: "WEBP" plus the chunks.
        out.extend_from_slice(&((self.0.len() + 4) as u32).to_le_bytes());
        out.extend_from_slice(b"WEBP");
        out.extend_from_slice(&self.0);
        out
    }

    /// A RIFF whose declared size overruns the bytes that follow it.
    fn build_with_declared_size(self, declared: u32) -> Vec<u8> {
        let mut out = b"RIFF".to_vec();
        out.extend_from_slice(&declared.to_le_bytes());
        out.extend_from_slice(b"WEBP");
        out.extend_from_slice(&self.0);
        out
    }
}

// -------------------------------------------------------------------- sniffing

#[test]
fn tiny_and_empty_inputs_are_unknown() {
    for len in [0usize, 1, 3, 8] {
        let bytes = vec![0u8; len];
        assert_eq!(
            scan_all(&bytes),
            (
                EXIFContainer::Unknown,
                EXIFScanOutcome::UnsupportedContainer
            ),
            "length {len}"
        );
    }
}

#[test]
fn random_bytes_are_unknown() {
    let bytes = b"this is not an image file at all";
    assert_eq!(scan_all(bytes).0, EXIFContainer::Unknown);
}

#[test]
fn big_tiff_is_not_misread_as_tiff() {
    for magic in [&b"II\x2B\x00"[..], &b"MM\x00\x2B"[..]] {
        let mut bytes = magic.to_vec();
        bytes.resize(64, 0);
        assert_eq!(
            scan_all(&bytes),
            (
                EXIFContainer::BigTIFF,
                EXIFScanOutcome::UnsupportedContainer
            ),
            "{magic:?}"
        );
    }
}

#[test]
fn gif_needs_its_whole_signature() {
    assert_eq!(scan_all(b"GIF87a\0\0\0\0").0, EXIFContainer::GIF);
    assert_eq!(scan_all(b"GIF89a\0\0\0\0").0, EXIFContainer::GIF);
    // An impostor sharing only the first four bytes.
    assert_eq!(scan_all(b"GIF8\0\0\0\0\0\0").0, EXIFContainer::Unknown);
}

#[test]
fn recognises_formats_that_cannot_carry_exif() {
    for (bytes, expected) in [
        (&b"GIF89a\0\0\0\0"[..], EXIFContainer::GIF),
        (&b"BM\0\0\0\0\0\0\0\0"[..], EXIFContainer::BMP),
        (&b"\xFF\x0A\0\0\0\0\0\0"[..], EXIFContainer::JXL),
        (&b"\x00\x00\x00\x0CJXL \r\n\x87\n"[..], EXIFContainer::JXL),
        (&b"<svg xmlns=\"\">"[..], EXIFContainer::SVG),
        (&b"<?xml version=\"1.0\"?>"[..], EXIFContainer::SVG),
    ] {
        let (container, outcome) = scan_all(bytes);
        assert_eq!(container, expected, "{bytes:?}");
        assert_eq!(outcome, EXIFScanOutcome::UnsupportedContainer, "{bytes:?}");
    }
}

#[test]
fn iso_base_media_brands_split_avif_from_heif() {
    for brand in [&b"avif"[..], b"avis", b"avio", b"miaf"] {
        let mut bytes = b"\x00\x00\x00\x18ftyp".to_vec();
        bytes.extend_from_slice(brand);
        bytes.resize(32, 0);
        assert_eq!(scan_all(&bytes).0, EXIFContainer::AVIF, "{brand:?}");
    }
    for brand in [&b"heic"[..], b"heix", b"hevc", b"mif1", b"msf1"] {
        let mut bytes = b"\x00\x00\x00\x18ftyp".to_vec();
        bytes.extend_from_slice(brand);
        bytes.resize(32, 0);
        assert_eq!(scan_all(&bytes).0, EXIFContainer::HEIF, "{brand:?}");
    }
}

#[test]
fn only_the_major_brand_is_read() {
    // An AVIF that names an AV1 brand only among its compatible brands reads as
    // HEIF, which is the documented consequence of reading bytes 8..12 alone.
    let mut bytes = b"\x00\x00\x00\x18ftypmif1".to_vec();
    bytes.extend_from_slice(b"avif");
    bytes.resize(32, 0);
    assert_eq!(scan_all(&bytes).0, EXIFContainer::HEIF);
}

#[test]
fn heif_and_avif_are_identified_but_not_walked() {
    // Their EXIF is reached through an offset table into mdat, which only the
    // crate's own ISOBMFF reader can follow -- and that reader has an extent
    // fan-out costing seconds of CPU per file that no cheap pre-check of ours
    // can see. Counted, so their share stays visible, and left alone.
    for brand in [&b"heic"[..], b"avif"] {
        let mut bytes = b"\x00\x00\x00\x18ftyp".to_vec();
        bytes.extend_from_slice(brand);
        bytes.resize(32, 0);
        let (_, outcome) = scan_all(&bytes);
        assert_eq!(outcome, EXIFScanOutcome::UnsupportedContainer, "{brand:?}");
        assert_eq!(gps_of(&bytes), EXIFGPSPresence::None, "{brand:?}");
    }
}

// ------------------------------------------------------------------------ JPEG

#[test]
fn jpeg_with_exif_is_located_in_full() {
    let bytes = Jpeg::new()
        .exif_app1(&tiff_block_with_position())
        .sos()
        .build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::JPEG, EXIFScanOutcome::Complete)
    );
}

#[test]
fn jpeg_with_metadata_but_no_gps_is_still_located_in_full() {
    let bytes = Jpeg::new()
        .jfif_app0()
        .exif_app1(&tiff_block_without_position())
        .sos()
        .build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::JPEG, EXIFScanOutcome::Complete)
    );
}

#[test]
fn jpeg_with_no_exif_at_all() {
    let bytes = Jpeg::new().jfif_app0().sos().build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::JPEG, EXIFScanOutcome::NoEXIF)
    );
}

#[test]
fn jpeg_finds_exif_after_an_xmp_app1() {
    // The first APP1 is not the Exif one, so dispatching on the marker rather
    // than on the identifier code would stop here.
    let bytes = Jpeg::new()
        .xmp_app1()
        .exif_app1(&tiff_block_with_position())
        .sos()
        .build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::JPEG, EXIFScanOutcome::Complete)
    );
}

#[test]
fn jpeg_finds_exif_among_several_app1s() {
    let bytes = Jpeg::new()
        .jfif_app0()
        .xmp_app1()
        .exif_app1(&tiff_block_with_position())
        .xmp_app1()
        .sos()
        .build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::JPEG, EXIFScanOutcome::Complete)
    );
}

#[test]
fn jpeg_tolerates_fill_bytes() {
    let bytes = Jpeg::new()
        .raw(&[0xFF, 0xFF, 0xFF])
        .exif_app1(&tiff_block_with_position())
        .sos()
        .build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::JPEG, EXIFScanOutcome::Complete)
    );
}

#[test]
fn jpeg_tolerates_standalone_markers() {
    // TEM and RST0 carry no length field, so treating them as segments would
    // read the bytes that follow as a length.
    let bytes = Jpeg::new()
        .raw(&[0xFF, 0x01, 0xFF, 0xD0])
        .exif_app1(&tiff_block_with_position())
        .sos()
        .build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::JPEG, EXIFScanOutcome::Complete)
    );
}

#[test]
fn jpeg_segment_length_below_two_is_malformed() {
    // A length field counts itself, so one is impossible.
    let bytes = [0xFF, 0xD8, 0xFF, 0xE1, 0x00, 0x01];
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::JPEG, EXIFScanOutcome::MalformedContainer)
    );
}

#[test]
fn jpeg_segment_running_past_the_end_of_a_whole_file_is_malformed() {
    let bytes = [0xFF, 0xD8, 0xFF, 0xE0, 0xFF, 0xFF];
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::JPEG, EXIFScanOutcome::MalformedContainer)
    );
}

#[test]
fn jpeg_exif_app1_without_a_tiff_header_is_malformed() {
    let bytes = Jpeg::new().exif_app1(b"not a tiff header").sos().build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::JPEG, EXIFScanOutcome::MalformedContainer)
    );
}

#[test]
fn jpeg_segment_beyond_the_prefix_is_location_unknown() {
    let full = Jpeg::new()
        .jfif_app0()
        .exif_app1(&tiff_block_with_position())
        .sos()
        .build();
    // Eight bytes reaches into the APP0 segment and no further: we have not
    // ruled out that EXIF follows, so this must not read as a confident no.
    assert_eq!(
        scan_prefix(&full, 8),
        (EXIFContainer::JPEG, EXIFScanOutcome::EXIFLocationUnknown)
    );
}

#[test]
fn jpeg_exif_in_a_clipped_block_is_truncated() {
    let full = Jpeg::new()
        .padded_exif_app1(&tiff_block_with_position(), 8192)
        .sos()
        .build();
    // We know where the block is and that it is EXIF, but not all of it is
    // here. That is truncation, not malformation.
    assert_eq!(
        scan_prefix(&full, 256),
        (EXIFContainer::JPEG, EXIFScanOutcome::EXIFTruncated)
    );
}

#[test]
fn jpeg_truncation_sweep_reaches_a_verdict_at_every_length() {
    let full = Jpeg::new()
        .jfif_app0()
        .xmp_app1()
        .exif_app1(&tiff_block_with_position())
        .sos()
        .build();
    for n in 0..=full.len() {
        let (container, outcome) = scan_prefix(&full, n);
        // Nothing may panic, and a short read must never be reported as a
        // well-formed absence.
        if container == EXIFContainer::JPEG {
            assert_ne!(outcome, EXIFScanOutcome::NoEXIF, "prefix of {n}");
        }
    }
    assert_eq!(scan_all(&full).1, EXIFScanOutcome::Complete);
}

// ------------------------------------------------------------------------- PNG

#[test]
fn png_with_exif_is_located_in_full() {
    let bytes = Png::new()
        .ihdr()
        .chunk(*b"eXIf", &tiff_block_with_position())
        .idat(64)
        .iend()
        .build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::PNG, EXIFScanOutcome::Complete)
    );
}

#[test]
fn png_without_exif() {
    let bytes = Png::new().ihdr().idat(64).iend().build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::PNG, EXIFScanOutcome::NoEXIF)
    );
}

#[test]
fn png_tolerates_a_spurious_exif_id_code() {
    // eXIf is specified to carry a bare TIFF block, but some encoders prepend
    // the JPEG identifier code anyway.
    let mut payload = b"Exif\0\0".to_vec();
    payload.extend_from_slice(&tiff_block_with_position());
    let bytes = Png::new()
        .ihdr()
        .chunk(*b"eXIf", &payload)
        .idat(64)
        .iend()
        .build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::PNG, EXIFScanOutcome::Complete)
    );
}

#[test]
fn png_exif_after_idat_is_still_found() {
    let bytes = Png::new()
        .ihdr()
        .idat(64)
        .chunk(*b"eXIf", &tiff_block_with_position())
        .iend()
        .build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::PNG, EXIFScanOutcome::Complete)
    );
}

#[test]
fn png_raw_profile_is_reported_as_unsupported_encoding() {
    // ImageMagick's hex-encoded copy of a JPEG APP1 segment. Reporting this as
    // "no EXIF" would be a confident false negative.
    for keyword in [
        &b"Raw profile type exif\0\nexif\n 34\n0x"[..],
        &b"Raw profile type APP1\0\napp1\n 34\n0x"[..],
    ] {
        let bytes = Png::new()
            .ihdr()
            .chunk(*b"tEXt", keyword)
            .idat(64)
            .iend()
            .build();
        assert_eq!(
            scan_all(&bytes),
            (EXIFContainer::PNG, EXIFScanOutcome::UnsupportedEXIFEncoding),
            "{keyword:?}"
        );
    }
}

#[test]
fn png_chunk_length_with_the_top_bit_set_is_malformed() {
    let mut bytes = b"\x89PNG\r\n\x1A\n".to_vec();
    bytes.extend_from_slice(&0x8000_0000u32.to_be_bytes());
    bytes.extend_from_slice(b"IHDR");
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::PNG, EXIFScanOutcome::MalformedContainer)
    );
}

#[test]
fn png_exif_in_a_clipped_block_is_truncated() {
    // The chunk header is inside the prefix, so we know where the block is,
    // but the prefix ends before the coordinates. That is truncation, and the
    // GPS answer must not be a confident one.
    let block = tiff_block_with_position();
    let full = Png::new()
        .ihdr()
        .chunk(*b"eXIf", &block)
        .idat(64)
        .iend()
        .build();
    // Signature, the IHDR chunk, the eXIf chunk header, then forty bytes of
    // the block: past its IFDs, short of the coordinate values.
    let cut = 8 + (12 + 13) + 8 + 40;
    let result = scan(&full[..cut], full.len() as u64);
    assert_eq!(result.container, EXIFContainer::PNG);
    assert_eq!(result.outcome, EXIFScanOutcome::EXIFTruncated);
    assert_ne!(result.gps_presence, EXIFGPSPresence::Position);
    assert_eq!(scan_all(&full).1, EXIFScanOutcome::Complete);
}

#[test]
fn png_exif_beyond_the_prefix_is_location_unknown() {
    let full = Png::new()
        .ihdr()
        .chunk(*b"iCCP", &vec![0u8; 200 * 1024])
        .chunk(*b"eXIf", &tiff_block_with_position())
        .iend()
        .build();
    assert_eq!(
        scan_prefix(&full, 128 * 1024),
        (EXIFContainer::PNG, EXIFScanOutcome::EXIFLocationUnknown)
    );
    // The same file read in full does find it.
    assert_eq!(scan_all(&full).1, EXIFScanOutcome::Complete);
}

#[test]
fn png_whole_file_with_overrunning_exif_length_is_malformed() {
    // A declared length running past the end of a file we hold entirely is a
    // lie, not a short read. EXIFTruncated would wrongly suggest that reading
    // further would help.
    let bytes = Png::new()
        .ihdr()
        .lying_chunk(*b"eXIf", 1000, &tiff_block_with_position())
        .build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::PNG, EXIFScanOutcome::MalformedContainer)
    );
}

#[test]
fn png_exif_chunk_without_a_tiff_header_is_malformed() {
    let bytes = Png::new()
        .ihdr()
        .chunk(*b"eXIf", b"not a tiff header")
        .iend()
        .build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::PNG, EXIFScanOutcome::MalformedContainer)
    );
}

// ------------------------------------------------------------------------ TIFF

#[test]
fn bare_tiff_is_located_in_full() {
    let bytes = tiff_block_with_position();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::TIFF, EXIFScanOutcome::Complete)
    );
}

#[test]
fn clipped_tiff_is_not_a_confident_no() {
    let mut full = tiff_block_with_position();
    full.extend_from_slice(&[0u8; 4096]);
    assert_eq!(
        scan_prefix(&full, 16),
        (EXIFContainer::TIFF, EXIFScanOutcome::EXIFTruncated)
    );
}

// ------------------------------------------------------------------------ WebP

#[test]
fn webp_with_exif_is_located_in_full() {
    let bytes = WebP::new()
        .chunk(*b"VP8 ", &[0u8; 16])
        .chunk(*b"EXIF", &tiff_block_with_position())
        .build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::WebP, EXIFScanOutcome::Complete)
    );
}

#[test]
fn webp_without_exif_is_a_real_absence() {
    // The RIFF size says where the container ends, so reaching that point
    // without an EXIF chunk is authoritative.
    let bytes = WebP::new().chunk(*b"VP8 ", &[0u8; 16]).build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::WebP, EXIFScanOutcome::NoEXIF)
    );
}

#[test]
fn webp_honours_odd_chunk_padding() {
    // An odd-sized payload is followed by a padding byte the size field does
    // not count. Ignoring it would desynchronise the walk.
    let bytes = WebP::new()
        .chunk(*b"VP8 ", &[0u8; 15])
        .chunk(*b"EXIF", &tiff_block_with_position())
        .build();
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::WebP, EXIFScanOutcome::Complete)
    );
}

#[test]
fn webp_exif_in_a_clipped_block_is_truncated() {
    let block = tiff_block_with_position();
    let full = WebP::new()
        .chunk(*b"VP8 ", &[0u8; 16])
        .chunk(*b"EXIF", &block)
        .build();
    // RIFF header, the VP8 chunk, the EXIF chunk header, then forty bytes of
    // the block: past its IFDs, short of the coordinate values.
    let cut = 12 + (8 + 16) + 8 + 40;
    let result = scan(&full[..cut], full.len() as u64);
    assert_eq!(result.container, EXIFContainer::WebP);
    assert_eq!(result.outcome, EXIFScanOutcome::EXIFTruncated);
    assert_ne!(result.gps_presence, EXIFGPSPresence::Position);
    assert_eq!(scan_all(&full).1, EXIFScanOutcome::Complete);
}

#[test]
fn webp_exif_beyond_the_prefix_is_location_unknown() {
    let full = WebP::new()
        .chunk(*b"VP8 ", &vec![0u8; 200 * 1024])
        .chunk(*b"EXIF", &tiff_block_with_position())
        .build();
    assert_eq!(
        scan_prefix(&full, 128 * 1024),
        (EXIFContainer::WebP, EXIFScanOutcome::EXIFLocationUnknown)
    );
    assert_eq!(scan_all(&full).1, EXIFScanOutcome::Complete);
}

#[test]
fn webp_riff_size_below_the_form_type_is_malformed() {
    // The size field counts the "WEBP" form type after it, so a value under
    // four contradicts the header the sniffer matched. Without the guard the
    // walk would start past the declared end and report a confident absence.
    for declared in [0u32, 3] {
        let bytes = WebP::new()
            .chunk(*b"EXIF", &tiff_block_with_position())
            .build_with_declared_size(declared);
        assert_eq!(
            scan_all(&bytes),
            (EXIFContainer::WebP, EXIFScanOutcome::MalformedContainer),
            "declared {declared}"
        );
    }
}

#[test]
fn webp_declared_size_overrunning_a_whole_file_is_malformed() {
    let bytes = WebP::new()
        .chunk(*b"VP8 ", &[0u8; 16])
        .build_with_declared_size(1_000_000);
    assert_eq!(
        scan_all(&bytes),
        (EXIFContainer::WebP, EXIFScanOutcome::MalformedContainer)
    );
}

// --------------------------------------------------------------- fixtures
//
// Real encoder output with only the metadata block replaced, so that at least
// some coverage rests on bytes not authored by the same understanding of the
// format as the code reading them. See test/generate_exif_fixtures.py.

/// Asserts the whole triple for one fixture.
fn expect_fixture(
    name: &str,
    bytes: &[u8],
    container: EXIFContainer,
    outcome: EXIFScanOutcome,
    presence: EXIFGPSPresence,
) {
    assert!(!bytes.is_empty(), "{name} is empty");
    let result = scan(bytes, bytes.len() as u64);
    assert_eq!(result.container, container, "{name} container");
    assert_eq!(result.outcome, outcome, "{name} outcome");
    assert_eq!(result.gps_presence, presence, "{name} gps");
}

#[test]
fn real_jpeg_files() {
    for (name, bytes, presence) in [
        (
            "exif_gps.jpg",
            &include_bytes!("../../test/exif_gps.jpg")[..],
            EXIFGPSPresence::Position,
        ),
        // The same position written little endian, so the fixtures cover both
        // byte orders and not just the hand-built blocks.
        (
            "exif_gps_le.jpg",
            &include_bytes!("../../test/exif_gps_le.jpg")[..],
            EXIFGPSPresence::Position,
        ),
        (
            "exif_nogps.jpg",
            &include_bytes!("../../test/exif_nogps.jpg")[..],
            EXIFGPSPresence::None,
        ),
        // A partially stripped file: the GPS IFD survives, carrying only a
        // version.
        (
            "exif_gps_stripped.jpg",
            &include_bytes!("../../test/exif_gps_stripped.jpg")[..],
            EXIFGPSPresence::NoPosition,
        ),
        // Stripped in place by blanking the coordinates.
        (
            "exif_gps_zeroed.jpg",
            &include_bytes!("../../test/exif_gps_zeroed.jpg")[..],
            EXIFGPSPresence::ZeroPosition,
        ),
        // The Interoperability IFD, whose 0x0001 and 0x0002 collide with the
        // GPS tag numbers, against a real file rather than a constructed one.
        (
            "exif_interop.jpg",
            &include_bytes!("../../test/exif_interop.jpg")[..],
            EXIFGPSPresence::None,
        ),
    ] {
        expect_fixture(
            name,
            bytes,
            EXIFContainer::JPEG,
            EXIFScanOutcome::Complete,
            presence,
        );
    }
}

#[test]
fn real_png_files() {
    expect_fixture(
        "exif_gps.png",
        include_bytes!("../../test/exif_gps.png"),
        EXIFContainer::PNG,
        EXIFScanOutcome::Complete,
        EXIFGPSPresence::Position,
    );
    expect_fixture(
        "exif_nogps.png",
        include_bytes!("../../test/exif_nogps.png"),
        EXIFContainer::PNG,
        EXIFScanOutcome::NoEXIF,
        EXIFGPSPresence::None,
    );
}

#[test]
fn real_webp_files() {
    expect_fixture(
        "exif_gps.webp",
        include_bytes!("../../test/exif_gps.webp"),
        EXIFContainer::WebP,
        EXIFScanOutcome::Complete,
        EXIFGPSPresence::Position,
    );
    expect_fixture(
        "tree_blend.webp",
        include_bytes!("../../test/tree_blend.webp"),
        EXIFContainer::WebP,
        EXIFScanOutcome::NoEXIF,
        EXIFGPSPresence::None,
    );
}

// Both of these carry a real Exif item. They are not walked, so what they pin
// is that a file whose metadata we cannot reach is reported as unsupported
// rather than as an absence of GPS.

#[test]
fn real_heif_file() {
    expect_fixture(
        "tree_exif.heic",
        include_bytes!("../../test/tree_exif.heic"),
        EXIFContainer::HEIF,
        EXIFScanOutcome::UnsupportedContainer,
        EXIFGPSPresence::None,
    );
}

#[test]
fn real_avif_file() {
    expect_fixture(
        "tree_animated.avif",
        include_bytes!("../../test/tree_animated.avif"),
        EXIFContainer::AVIF,
        EXIFScanOutcome::UnsupportedContainer,
        EXIFGPSPresence::None,
    );
}

#[test]
fn pre_existing_tree_images_scan_cleanly() {
    // Third-party bytes nobody wrote for this test, copied unmodified from
    // image/test/gtest. Big endian, with a real Exif sub-IFD. The scan only has
    // to reach a conclusion rather than run off the end of the file.
    for (name, bytes) in [
        (
            "tree_exif_resolution.jpg",
            &include_bytes!("../../test/tree_exif_resolution.jpg")[..],
        ),
        (
            "tree_green.jpg",
            &include_bytes!("../../test/tree_green.jpg")[..],
        ),
        (
            "tree_green.png",
            &include_bytes!("../../test/tree_green.png")[..],
        ),
    ] {
        let result = scan(bytes, bytes.len() as u64);
        assert_ne!(result.container, EXIFContainer::Unknown, "{name}");
        assert_eq!(result.gps_presence, EXIFGPSPresence::None, "{name}");
        assert!(
            matches!(
                result.outcome,
                EXIFScanOutcome::Complete | EXIFScanOutcome::NoEXIF
            ),
            "{name} reached {:?}",
            result.outcome
        );
    }
}
