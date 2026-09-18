// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

//! Finds the EXIF block in an image file and reports whether it says where the
//! photo was taken.
//!
//! This exists to be run over files a user has just handed to a web page, so it
//! reads a bounded prefix rather than pulling whole images into memory to answer
//! one question about their header. The consequence is an asymmetry that callers
//! must respect:
//!
//! A POSITIVE result is always authoritative. A coordinate that has been
//! validated cannot be un-found by supplying more bytes, so a position is
//! reportable even when the block was clipped.
//!
//! A NEGATIVE result is only authoritative for `Complete` and `NoEXIF`. Under
//! any other outcome the absence of GPS means "we could not tell", not "there
//! is none", and must not be reported as a confident no.

#![deny(unsafe_op_in_unsafe_fn)]
#![deny(clippy::undocumented_unsafe_blocks)]
// The names of these types and their variants are a data contract: they reach
// telemetry as Glean labels, and the C++ that maps them spells them this way.
#![allow(clippy::upper_case_acronyms)]

mod container;
pub mod ffi;
