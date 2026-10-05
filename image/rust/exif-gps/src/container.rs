// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

//! Locating the EXIF block within an image file.
//!
//! Only JPEG, PNG, TIFF and WebP are walked. Other containers are identified so
//! that their share stays visible in the data, but not opened.

use crate::ffi::{EXIFContainer, EXIFGPSPresence, EXIFScanOutcome, EXIFScanResult};
use crate::gps;

/// The identifier code that introduces EXIF in a JPEG APP1 segment. See EXIF
/// Section 4.7.2.
const EXIF_ID_CODE: &[u8] = b"Exif\0\0";

/// Everything a locator needs to know about the bytes it was handed.
struct ScanContext<'a> {
    bytes: &'a [u8],
    /// Whether `bytes` covers the file to its end. This is what separates a
    /// genuinely truncated file, which is malformed, from a well-formed one we
    /// have simply not read enough of.
    have_whole_file: bool,
}

impl<'a> ScanContext<'a> {
    /// What running out of bytes means, given how much of the file we hold.
    fn ran_out(&self) -> EXIFScanOutcome {
        if self.have_whole_file {
            EXIFScanOutcome::MalformedContainer
        } else {
            EXIFScanOutcome::EXIFLocationUnknown
        }
    }

    /// The part of `start .. start + length` we actually have.
    fn available(&self, start: usize, length: u64) -> &'a [u8] {
        if start >= self.bytes.len() {
            return &[];
        }
        let remaining = (self.bytes.len() - start) as u64;
        let take = length.min(remaining) as usize;
        &self.bytes[start..start + take]
    }

    /// Whether an offset arrived at by adding a declared length is still inside
    /// the bytes we hold.
    fn within(&self, offset: u64) -> bool {
        offset <= self.bytes.len() as u64
    }
}

/// What a locator concluded: how the scan went, and what the metadata said.
struct Verdict {
    outcome: EXIFScanOutcome,
    gps_presence: EXIFGPSPresence,
}

impl Verdict {
    /// A conclusion reached without reading an EXIF block.
    fn outcome(outcome: EXIFScanOutcome) -> Self {
        Self {
            outcome,
            gps_presence: EXIFGPSPresence::None,
        }
    }
}

/// "II" 42 or "MM" 42, the two ways a TIFF header can begin. See TIFF 6.0
/// Section 2, and EXIF Section 4.5.2.
fn starts_with_tiff_magic(bytes: &[u8]) -> bool {
    bytes.starts_with(b"II\x2A\x00") || bytes.starts_with(b"MM\x00\x2A")
}

/// Finds the TIFF header inside a located EXIF block, returning its offset.
///
/// Containers disagree about how an EXIF block is framed, and encoders disagree
/// with the containers:
///
/// - A JPEG APP1 segment begins with the "Exif\0\0" identifier code, per
///   Section 4.7.2.
/// - A PNG eXIf chunk is specified not to carry that identifier, yet some
///   encoders write it anyway.
///
/// Rather than trust the container, look for the TIFF header that has to follow
/// either way.
fn find_tiff_header(block: &[u8]) -> Option<usize> {
    if starts_with_tiff_magic(block) {
        return Some(0);
    }
    if block.starts_with(EXIF_ID_CODE) && starts_with_tiff_magic(&block[EXIF_ID_CODE.len()..]) {
        return Some(EXIF_ID_CODE.len());
    }
    None
}

/// Reaches a verdict about an EXIF block the container pointed us at.
///
/// `complete` says whether all of the block's declared length is present.
fn finish_with_exif(ctx: &ScanContext, block: &[u8], complete: bool) -> Verdict {
    // What a block we do not have in full means. With the whole file in hand its
    // declared length overran the file, which is malformed; otherwise we have
    // simply not read far enough, which is truncation and says nothing either
    // way.
    let incomplete = if ctx.have_whole_file {
        EXIFScanOutcome::MalformedContainer
    } else {
        EXIFScanOutcome::EXIFTruncated
    };

    // We are only here because the container said this was EXIF, so bytes that
    // are not a TIFF header mean the file is inconsistent with itself. If we do
    // not have the whole block yet, we may simply not have reached the header.
    let Some(tiff_start) = find_tiff_header(block) else {
        return Verdict::outcome(if complete {
            EXIFScanOutcome::MalformedContainer
        } else {
            incomplete
        });
    };

    match gps::classify(&block[tiff_start..]) {
        // A block present in full that did not parse as a TIFF structure tells
        // us nothing about GPS, so it must not read as a confident absence.
        // When we do not have all of the block the failure may be for lack of
        // bytes, which is truncation rather than malformation.
        gps::Parsed::Unparseable => Verdict::outcome(if complete {
            EXIFScanOutcome::MalformedContainer
        } else {
            incomplete
        }),

        // The outcome describes what happened to the scan, not what it found.
        // Callers combine the two: a position is conclusive however little of
        // the block we had, whereas the absence of one only means something if
        // we had all of it.
        gps::Parsed::Walked(gps_presence) => Verdict {
            outcome: if complete {
                EXIFScanOutcome::Complete
            } else {
                incomplete
            },
            gps_presence,
        },
    }
}

/// JPEG. ISO/IEC 10918-1 marker syntax; EXIF Section 4.7.2 for the APP1 layout.
fn scan_jpeg(ctx: &ScanContext) -> Verdict {
    let bytes = ctx.bytes;

    // Past the SOI marker that the sniffer already matched.
    let mut offset = 2usize;

    loop {
        // Any number of 0xFF bytes may pad the gap before a marker.
        while offset < bytes.len() && bytes[offset] == 0xFF {
            offset += 1;
        }
        if offset >= bytes.len() {
            return Verdict::outcome(ctx.ran_out());
        }

        let marker = bytes[offset];
        offset += 1;

        // Standalone markers: TEM and the restart markers carry no payload.
        if marker == 0x01 || (0xD0..=0xD7).contains(&marker) {
            continue;
        }

        // Start of scan, or end of image. Entropy-coded data follows SOS and no
        // further metadata segment can precede the image after that point.
        if marker == 0xDA || marker == 0xD9 {
            return Verdict::outcome(EXIFScanOutcome::NoEXIF);
        }

        if offset + 2 > bytes.len() {
            return Verdict::outcome(ctx.ran_out());
        }

        // The length field counts its own two bytes.
        let segment_length = u16::from_be_bytes([bytes[offset], bytes[offset + 1]]);
        if segment_length < 2 {
            return Verdict::outcome(EXIFScanOutcome::MalformedContainer);
        }
        let payload_start = offset + 2;
        let payload_length = u64::from(segment_length - 2);

        // APP1 is where EXIF lives, but it is also where XMP lives, and a photo
        // commonly carries both in either order. Dispatching on the identifier
        // string rather than on the marker is what tells them apart.
        if marker == 0xE1 {
            let payload = ctx.available(payload_start, payload_length);

            if payload.starts_with(EXIF_ID_CODE) {
                let complete = payload.len() as u64 == payload_length;
                return finish_with_exif(ctx, payload, complete);
            }

            // Not enough of the payload to read the identifier, so we cannot
            // rule out that this was the EXIF segment.
            if payload.len() < EXIF_ID_CODE.len() && (payload.len() as u64) < payload_length {
                return Verdict::outcome(ctx.ran_out());
            }
        }

        let Some(next) = (payload_start as u64).checked_add(payload_length) else {
            return Verdict::outcome(ctx.ran_out());
        };
        if !ctx.within(next) {
            return Verdict::outcome(ctx.ran_out());
        }
        offset = next as usize;
    }
}

/// Rather than writing an eXIf chunk, ImageMagick may hex-encode a JPEG APP1
/// segment into a tEXt or zTXt chunk under one of these keywords: version 6
/// names the profile after the marker and so takes this path for every JPEG it
/// converts, and versions predating eXIf support took it for "exif" too.
/// Reporting either as "no EXIF" would be a confident false negative.
fn has_raw_profile_keyword(chunk_data: &[u8]) -> bool {
    chunk_data.starts_with(b"Raw profile type exif")
        || chunk_data.starts_with(b"Raw profile type APP1")
}

/// PNG. The eXIf chunk, added in the 1.5.0 extensions and normative in the PNG
/// Specification (Third Edition), carries a bare TIFF block with no identifier
/// code.
fn scan_png(ctx: &ScanContext) -> Verdict {
    let bytes = ctx.bytes;

    // Past the 8-byte signature that the sniffer already matched.
    let mut offset = 8usize;

    loop {
        // Chunk header: a 4-byte big-endian length followed by a 4-byte type.
        if offset + 8 > bytes.len() {
            return Verdict::outcome(ctx.ran_out());
        }

        let data_length = u32::from_be_bytes([
            bytes[offset],
            bytes[offset + 1],
            bytes[offset + 2],
            bytes[offset + 3],
        ]);
        let chunk_type = &bytes[offset + 4..offset + 8];
        let data_start = offset + 8;

        // A chunk length is a four-byte unsigned integer whose value may not
        // exceed 2^31-1, so the top bit must be clear. See PNG Specification
        // (Third Edition), 5.3, Table 5.
        if data_length > 0x7FFF_FFFF {
            return Verdict::outcome(EXIFScanOutcome::MalformedContainer);
        }

        if chunk_type == b"eXIf" {
            let payload = ctx.available(data_start, u64::from(data_length));
            let complete = payload.len() as u64 == u64::from(data_length);
            return finish_with_exif(ctx, payload, complete);
        }

        if (chunk_type == b"tEXt" || chunk_type == b"zTXt")
            && has_raw_profile_keyword(ctx.available(data_start, u64::from(data_length)))
        {
            return Verdict::outcome(EXIFScanOutcome::UnsupportedEXIFEncoding);
        }

        if chunk_type == b"IEND" {
            return Verdict::outcome(EXIFScanOutcome::NoEXIF);
        }

        // The data, then a 4-byte CRC. CRCs are not verified: we are locating
        // metadata, not decoding an image, and a bad CRC would not change where
        // the next chunk starts.
        let Some(next) = (data_start as u64)
            .checked_add(u64::from(data_length))
            .and_then(|n| n.checked_add(4))
        else {
            return Verdict::outcome(ctx.ran_out());
        };
        if !ctx.within(next) {
            return Verdict::outcome(ctx.ran_out());
        }
        offset = next as usize;
    }
}

/// WebP. A RIFF container whose metadata chunks follow the image data, so a
/// prefix commonly stops short of them. See RFC 9649.
fn scan_webp(ctx: &ScanContext) -> Verdict {
    let bytes = ctx.bytes;

    // The sniffer matched "RIFF", a size, and "WEBP". The size counts
    // everything after itself, so it says where the container ends; walking
    // past that point would be reading whatever the file happens to carry
    // afterwards.
    let riff_size = u32::from_le_bytes([bytes[4], bytes[5], bytes[6], bytes[7]]);
    // The size covers the "WEBP" form type the sniffer just matched, so a
    // smaller one contradicts the header it sits in.
    if riff_size < 4 {
        return Verdict::outcome(EXIFScanOutcome::MalformedContainer);
    }
    let container_end = 8u64 + u64::from(riff_size);

    let mut offset = 12usize;

    loop {
        // Reaching the declared end without an EXIF chunk is a real absence: the
        // container told us where it stopped and we got there.
        if offset as u64 >= container_end {
            return Verdict::outcome(EXIFScanOutcome::NoEXIF);
        }

        // Chunk header: a 4-byte type followed by a 4-byte little-endian size.
        if offset + 8 > bytes.len() {
            return Verdict::outcome(ctx.ran_out());
        }

        let fourcc = &bytes[offset..offset + 4];
        let chunk_size = u32::from_le_bytes([
            bytes[offset + 4],
            bytes[offset + 5],
            bytes[offset + 6],
            bytes[offset + 7],
        ]);
        let data_start = offset + 8;

        if fourcc == b"EXIF" {
            let payload = ctx.available(data_start, u64::from(chunk_size));
            let complete = payload.len() as u64 == u64::from(chunk_size);
            return finish_with_exif(ctx, payload, complete);
        }

        // A chunk's payload is padded to an even length, and the padding byte is
        // not counted by the size field.
        let padded = u64::from(chunk_size) + u64::from(chunk_size & 1);
        let Some(next) = (data_start as u64).checked_add(padded) else {
            return Verdict::outcome(ctx.ran_out());
        };
        if !ctx.within(next) {
            return Verdict::outcome(ctx.ran_out());
        }
        offset = next as usize;
    }
}

/// Identifies a container from its leading bytes.
fn sniff(bytes: &[u8]) -> EXIFContainer {
    if bytes.starts_with(b"\xFF\xD8\xFF") {
        return EXIFContainer::JPEG;
    }
    if bytes.starts_with(b"\x89PNG\r\n\x1A\n") {
        return EXIFContainer::PNG;
    }
    if starts_with_tiff_magic(bytes) {
        return EXIFContainer::TIFF;
    }
    if bytes.starts_with(b"II\x2B\x00") || bytes.starts_with(b"MM\x00\x2B") {
        return EXIFContainer::BigTIFF;
    }
    if bytes.starts_with(b"RIFF") && bytes.len() >= 12 && bytes[8..12] == *b"WEBP" {
        return EXIFContainer::WebP;
    }
    if bytes.starts_with(b"GIF87a") || bytes.starts_with(b"GIF89a") {
        return EXIFContainer::GIF;
    }
    if bytes.starts_with(b"BM") {
        return EXIFContainer::BMP;
    }
    // A JPEG XL container, or a naked codestream.
    if bytes.starts_with(b"\x00\x00\x00\x0CJXL \r\n\x87\n") || bytes.starts_with(b"\xFF\x0A") {
        return EXIFContainer::JXL;
    }

    // ISO base media files put a box length in the first four bytes and the
    // "ftyp" box type in the next four; the brand that follows says which
    // dialect. Only the major brand is read, so an AVIF that declares another
    // brand as its major one and names an AV1 brand only among its compatible
    // brands reads as HEIF. See ISO/IEC 14496-12, ISO/IEC 23008-12, and the AV1
    // Image File Format specification.
    if bytes.len() >= 12 && bytes[4..8] == *b"ftyp" {
        let brand = &bytes[8..12];
        if brand == b"avif" || brand == b"avis" || brand == b"avio" || brand == b"miaf" {
            return EXIFContainer::AVIF;
        }
        return EXIFContainer::HEIF;
    }

    if bytes.starts_with(b"<?xml") || bytes.starts_with(b"<svg") {
        return EXIFContainer::SVG;
    }

    EXIFContainer::Unknown
}

/// Scans a prefix of an image file for GPS metadata. See the crate docs for the
/// authority rules a caller must respect.
pub(crate) fn scan(bytes: &[u8], file_size: u64) -> EXIFScanResult {
    let container = sniff(bytes);
    let ctx = ScanContext {
        bytes,
        have_whole_file: bytes.len() as u64 >= file_size,
    };

    let verdict = match container {
        EXIFContainer::JPEG => scan_jpeg(&ctx),
        EXIFContainer::PNG => scan_png(&ctx),
        EXIFContainer::WebP => scan_webp(&ctx),
        // The file is the EXIF block; there is no container to walk. This is
        // also where the camera raw formats built on TIFF arrive: DNG, NEF, ARW
        // and CR2 carry TIFF magic and keep their GPS in the standard IFDs, so
        // they are located here rather than needing a container of their own.
        EXIFContainer::TIFF => finish_with_exif(&ctx, bytes, ctx.have_whole_file),
        EXIFContainer::Unknown
        | EXIFContainer::BigTIFF
        | EXIFContainer::HEIF
        | EXIFContainer::AVIF
        | EXIFContainer::GIF
        | EXIFContainer::BMP
        | EXIFContainer::JXL
        | EXIFContainer::SVG => Verdict::outcome(EXIFScanOutcome::UnsupportedContainer),
    };

    EXIFScanResult {
        container,
        outcome: verdict.outcome,
        gps_presence: verdict.gps_presence,
    }
}

#[cfg(test)]
mod tests;
