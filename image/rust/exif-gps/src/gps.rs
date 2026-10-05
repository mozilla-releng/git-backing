// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

//! Classifying what a located EXIF block says about where a photo was taken.
//!
//! Only the classification leaves this module. A coordinate is decoded in order
//! to decide whether it is one, and is then dropped: the sign and the altitude
//! are therefore never read, because a position is a position in either
//! hemisphere and at any height.

use exif::{Context, Error, Reader, Tag, Value};

use crate::ffi::EXIFGPSPresence;

/// What the parser was able to say about a block.
pub(crate) enum Parsed {
    /// The block was walked, at least in part, and this is what it said.
    Walked(EXIFGPSPresence),
    /// The block could not be walked at all, so it says nothing either way. In
    /// particular it must not be reported as an absence of GPS.
    Unparseable,
}

/// Walks a TIFF block and classifies its GPS content.
///
/// `tiff` must begin at the TIFF header.
pub(crate) fn classify(tiff: &[u8]) -> Parsed {
    // Known issue: kamadak-exif 0.6.1 does not bound what a crafted block can
    // cost. Repeated IFD pointers multiply the entries it pushes, and entries
    // that each declare the whole block as their value are all decoded by
    // Exif::fields. Accidental malformation does not produce either shape, so
    // this is accepted rather than guarded; see the crate's audit notes.
    //
    // continue_on_error keeps whatever could be read. That matters twice over: a
    // coordinate whose value bytes fall outside a clipped block should not
    // discard the entries that parsed, and "some fields and some errors" is a
    // different thing from "nothing at all", which is what lets a truncated
    // block be told from a malformed one.
    let exif = match Reader::new()
        .continue_on_error(true)
        .read_raw(tiff.to_vec())
    {
        Ok(exif) => exif,
        Err(Error::PartialResult(partial)) => {
            let (exif, _errors) = partial.into_inner();
            if exif.fields().next().is_none() {
                return Parsed::Unparseable;
            }
            exif
        }
        Err(_) => return Parsed::Unparseable,
    };

    Parsed::Walked(presence(&exif))
}

/// Reduces a parsed block to whether it says where the photo was taken.
fn presence(exif: &exif::Exif) -> EXIFGPSPresence {
    let mut saw_gps_ifd = false;
    let mut latitude = None;
    let mut longitude = None;

    // Every IFD counts, not only the primary one: the question is whether the
    // file carries a position, and a thumbnail IFD's GPS is still a position in
    // the file. Tag numbers are namespaced by context here, which is what stops
    // the Interoperability IFD -- whose 0x0001 and 0x0002 are
    // InteroperabilityIndex and InteroperabilityVersion, present in the large
    // majority of camera JPEGs -- from reading as a latitude and a longitude.
    for field in exif.fields() {
        if field.tag.context() != Context::Gps {
            continue;
        }

        // Reached the GPS attribute IFD. Recorded even when there turns out to
        // be no position in it, because the difference between "never had one"
        // and "had one removed" is worth keeping.
        saw_gps_ifd = true;

        if field.tag == Tag::GPSLatitude {
            latitude = latitude.or_else(|| degrees(&field.value, 90));
        } else if field.tag == Tag::GPSLongitude {
            longitude = longitude.or_else(|| degrees(&field.value, 180));
        }
        // GPSDestLatitude and GPSDestLongitude say where the camera was pointed,
        // not where it stood, so they are not a position. Every other GPS tag
        // counts only towards having seen the IFD.
    }

    match (latitude, longitude) {
        // Both coordinates exactly zero is Null Island, in the Gulf of Guinea:
        // in practice a blanked coordinate rather than anywhere anyone stood.
        // Latitude alone being zero is ordinary -- that is just the equator --
        // so both have to be zero.
        (Some(lat), Some(lon)) if lat == 0.0 && lon == 0.0 => EXIFGPSPresence::ZeroPosition,
        (Some(_), Some(_)) => EXIFGPSPresence::Position,
        _ if saw_gps_ifd => EXIFGPSPresence::NoPosition,
        _ => EXIFGPSPresence::None,
    }
}

/// A coordinate as an unsigned magnitude in decimal degrees, if the entry is
/// one at all.
///
/// `max_degrees` bounds the degrees component: 90 for a latitude, 180 for a
/// longitude.
fn degrees(value: &Value, max_degrees: u32) -> Option<f64> {
    const WEIGHTS: [f64; 3] = [1.0, 1.0 / 60.0, 1.0 / 3600.0];

    // Section 4.6.7 (4.6.6 in EXIF v2.3): three RATIONALs, giving degrees,
    // minutes and seconds.
    //
    // Components are taken as (numerator, denominator) magnitudes rather than
    // through Rational::to_f64 so that the signed and unsigned forms share one
    // path, and so that the zero-denominator check below happens before any
    // division: to_f64 divides unchecked, which would turn 51/0 into infinity
    // and 0/0 into a NaN that compares false against everything, including the
    // zero test above.
    let components: [(u32, u32); 3] = match value {
        Value::Rational(v) if v.len() == 3 => [
            (v[0].num, v[0].denom),
            (v[1].num, v[1].denom),
            (v[2].num, v[2].denom),
        ],
        // AOSP's camera stack writes SRATIONAL here, which the specification
        // does not allow. Take the magnitude: the sign of a component is
        // meaningless for a coordinate whose sign lives in a separate tag.
        Value::SRational(v) if v.len() == 3 => [
            (v[0].num.unsigned_abs(), v[0].denom.unsigned_abs()),
            (v[1].num.unsigned_abs(), v[1].denom.unsigned_abs()),
            (v[2].num.unsigned_abs(), v[2].denom.unsigned_abs()),
        ],
        _ => return None,
    };

    // Range-check each component: an entry with a bad value offset points at
    // unrelated bytes, which still decode as rationals. Compare the divided
    // value, not the numerator, since encoders often put the fraction in the
    // minutes or seconds (305/10 minutes, 3900/100 seconds).
    let mut total = 0.0;
    for (i, (num, denom)) in components.into_iter().enumerate() {
        if denom == 0 {
            return None;
        }
        let component = f64::from(num) / f64::from(denom);
        let in_range = if i == 0 {
            component <= f64::from(max_degrees)
        } else {
            component < 60.0
        };
        if !in_range {
            return None;
        }
        total += component * WEIGHTS[i];
    }

    Some(total)
}

#[cfg(test)]
mod tests;
