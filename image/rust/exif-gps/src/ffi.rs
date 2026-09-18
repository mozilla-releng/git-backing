/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::slice;

/// The container format a file's leading bytes identify it as. The names of
/// these reach telemetry, so treat them as a data contract.
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EXIFContainer {
    Unknown,
    JPEG,
    PNG,
    TIFF,
    /// II 2B 00 / MM 00 2B. A different header layout to classic TIFF, so it
    /// must be recognised separately rather than mis-parsed as one.
    BigTIFF,
    WebP,
    HEIF,
    AVIF,
    /// The container form can carry EXIF in an `Exif` box, which we do not
    /// locate. A bare codestream cannot.
    JXL,
    /// Recognised, but cannot carry EXIF at all.
    GIF,
    BMP,
    SVG,
}

/// Why a scan produced the GPS answer it did.
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EXIFScanOutcome {
    /// The EXIF block was found and walked in full.
    Complete,
    /// The container is well formed and carries no EXIF block at all.
    NoEXIF,
    /// A format whose EXIF we do not locate, or none we recognise.
    UnsupportedContainer,
    /// Segment, chunk or block lengths are inconsistent.
    MalformedContainer,
    /// EXIF is present but in an encoding we cannot read. Distinguished from
    /// `NoEXIF` so that it does not read as a confident "no GPS".
    UnsupportedEXIFEncoding,
    /// We know where the EXIF block is but did not have all of it.
    EXIFTruncated,
    /// We ran out of bytes before locating anything.
    EXIFLocationUnknown,
}

/// Whether the metadata can say where the image was captured.
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EXIFGPSPresence {
    /// No GPS attribute IFD was reached.
    None,
    /// A GPS attribute IFD exists but carries no usable position -- commonly a
    /// GPSVersionID and nothing else.
    NoPosition,
    /// Both coordinates are present and well formed, and both are exactly zero:
    /// Null Island, in the Gulf of Guinea.
    ZeroPosition,
    /// Both GPSLatitude and GPSLongitude are present, well formed, and not both
    /// zero.
    Position,
}

/// The result of one scan.
///
/// Deliberately carries no coordinate. A position is decoded internally in
/// order to classify it and is then dropped; only the classification crosses
/// this boundary, so a location cannot be recorded, logged or forwarded by any
/// caller. See bug 2058913.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct EXIFScanResult {
    pub container: EXIFContainer,
    pub outcome: EXIFScanOutcome,
    pub gps_presence: EXIFGPSPresence,
}

impl Default for EXIFScanResult {
    fn default() -> Self {
        Self {
            container: EXIFContainer::Unknown,
            outcome: EXIFScanOutcome::UnsupportedContainer,
            gps_presence: EXIFGPSPresence::None,
        }
    }
}

/// Scans a prefix of an image file for GPS metadata.
///
/// `bytes` must start at file offset 0 and may be a prefix of the file;
/// `file_size` is the full size, which is how the scan tells "the file ends
/// here" apart from "our buffer ends here".
///
/// This is pure computation over bytes: no I/O, no allocation of the input and
/// no threading assumptions. Restricting the scanning of user-supplied files to
/// content processes is the caller's job.
///
/// # Safety
/// `bytes` must point to at least `len` readable bytes, or be null when `len`
/// is zero.
#[no_mangle]
pub unsafe extern "C" fn exif_gps_scan(
    bytes: *const u8,
    len: usize,
    file_size: u64,
) -> EXIFScanResult {
    // A zero length is not an error, but `from_raw_parts` is undefined on a
    // null pointer even then, so it must not be reached with one.
    if bytes.is_null() || len == 0 {
        return EXIFScanResult::default();
    }

    // SAFETY: the caller guarantees `bytes` points to `len` readable bytes, and
    // the null case returned above.
    let slice = unsafe { slice::from_raw_parts(bytes, len) };
    crate::container::scan(slice, file_size)
}
