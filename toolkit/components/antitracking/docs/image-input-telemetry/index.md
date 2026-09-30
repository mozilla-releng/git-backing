# Image Input Telemetry

Image input telemetry measures how often the image files users hand to web
pages carry EXIF metadata that says where the photo was taken. It exists to
decide whether stripping that metadata is worth shipping (bug 2058913).

The coordinates themselves are never recorded. The scanner decodes a position
only to decide whether it is one, and drops it before anything crosses back
into C++.

## What is recorded

Both metrics are defined in `toolkit/components/antitracking/imageinput/metrics.yaml`.

- `image_input.offered`: an event per image file, with four extras:
  - `input_type`: `file_picker`, `directory_picker`, `drop` or `paste`.
  - `detected_format`: the container sniffed from the file's leading bytes, not
    the type the platform reported.
  - `has_gps`: `true`, `ifd_only`, `false` or `unknown`.
  - `scan_outcome`: why `has_gps` has the value it does, including the reasons
    a file was not scanned.
- `image_input.batch_size`: a distribution of how many candidate images each
  user action handed over, recorded uncapped.

## When it is recorded

A file is counted at the moment it is handed to the page, since that is when
page script gains read access to its bytes, whether or not it is ever
uploaded. The hooks are:

- **File and folder pickers:** `HTMLInputElement`, when the picker returns.
- **Drop:** `DragEvent`, for trusted `drop` events only. A drop that a page
  dispatches at itself moves no file across any boundary.
- **Paste:** `ClipboardEvent`.

Recording is skipped entirely unless:

- `privacy.imageInputTelemetry.enabled` is true, and
- the receiving document is web content: an `http`, `https` or `file`
  content principal, or a null principal (sandboxed iframes, `data:`).
  System principals, `about:` pages and extensions are never recorded.

A file counts as a candidate if its MIME type starts with `image/` or its name
has a known image extension. The extension fallback matters for HEIC, which on
some platforms reports an empty MIME type.

## How a file is scanned

1. `ImageInputTelemetry` filters the handed-over files to candidates and
   applies the per-action caps. The first 8 candidates are scanned, the next
   ones up to 32 get an event marked `skipped_batch_limit`, and anything past
   32 is only counted in `batch_size`.
2. Images the platform holds as pixels rather than as a file, such as a
   screenshot on the clipboard, are recorded as `skipped_bitmap` without being
   turned into a `File`. Materializing one would decode and re-encode the image
   synchronously on the main thread.
3. `ImageInputScan` reads at most the first 128 KiB of each file through
   `ImageInputBlobReader`, off the main thread, on one shared serial task
   queue. The read uses a sliced clone of the blob, so the stream the page will
   later upload is never touched.
4. The bytes are passed to the `exif-gps` Rust crate (`image/rust/exif-gps`)
   through a single FFI call, `exif_gps_scan`. It sniffs the container, finds
   the EXIF block itself for JPEG, PNG, WebP and TIFF, and hands only that
   block to the vendored `kamadak-exif` crate to walk the IFDs.
5. The crate returns three enums and no coordinates. `ImageInputLabels` maps
   them to the event's string extras, and one `image_input.offered` event is
   recorded.

File bytes are only ever read in a content process. A release assertion in
`ImageInputScan` enforces this.

The path of one file, from the DOM hook that sees it to the events it
produces:

```{mermaid}
flowchart TD
    PICKER["File or folder picker<br/>HTMLInputElement"]
    DROPPASTE["Drop or paste<br/>DragEvent, ClipboardEvent"]
    FILES["MaybeRecordFiles"]
    DT["MaybeRecordDataTransfer"]
    GATE["ShouldRecord<br/>pref, principal"]
    SEL["RecordSelection<br/>candidates and caps"]
    START["ImageInputScan::Start"]
    SKIP["ImageInputScan::<br/>RecordSkipped"]
    BATCH[("image_input.<br/>batch_size")]
    READER["ImageInputBlobReader<br/>first 128 KiB"]
    ONBYTES["ImageInputScan::OnBytes"]
    RUST["exif_gps_scan<br/>Rust, see below"]
    LABELS["ImageInputLabels"]
    OFFERED[("image_input.offered")]

    PICKER --> FILES --> GATE
    DROPPASTE --> DT --> GATE
    GATE --> SEL
    SEL -->|"first 8"| START
    SEL -->|"skipped"| SKIP
    SEL -->|"count"| BATCH
    START --> READER
    READER -->|"scan queue"| ONBYTES
    ONBYTES --> RUST
    RUST -->|"EXIFScanResult"| LABELS
    LABELS --> OFFERED
    SKIP --> OFFERED
```

### Inside the exif-gps crate

`ffi.rs` is the only entry point and the only `unsafe` code. `container.rs`
finds the EXIF block, and `gps.rs` classifies it.

```{mermaid}
flowchart TD
    CPP["ImageInputScan::OnBytes (C++)"]
    SCAN_FFI["exif_gps_scan<br/>ffi.rs"]

    subgraph container["container.rs"]
        SCAN["scan<br/>sniff the format"]
        WALK["scan_jpeg<br/>scan_png<br/>scan_webp"]
        UNSUP(["unsupported_container"])
        FINISH["finish_with_exif<br/>find the TIFF header"]
    end

    subgraph gps["gps.rs"]
        CLASSIFY["classify"]
        PRESENCE["presence"]
        DEGREES["degrees"]
    end

    READRAW["kamadak-exif<br/>Reader::read_raw"]

    CPP --> SCAN_FFI --> container
    SCAN -->|"JPEG, PNG, WebP"| WALK
    SCAN -->|"TIFF"| FINISH
    SCAN -->|"other"| UNSUP
    WALK -->|"EXIF block"| FINISH
    FINISH --> gps
    CLASSIFY -->|"parse"| READRAW
    CLASSIFY --> PRESENCE
    PRESENCE -->|"each coordinate"| DEGREES
```

An arrow into a box enters at the function at its top. The rounded node is an
outcome rather than a function, reached for HEIF, AVIF, JPEG XL, BigTIFF, GIF,
BMP, SVG and anything unrecognized.

To stay readable, the diagram shows only the path to a parsed EXIF block. Each
walker also bounds its reads through `ScanContext`, and can stop early with its
own `Verdict::outcome` when it finds no EXIF, runs out of bytes, or meets an
inconsistent length. `scan_png` additionally reports `unsupported_exif_encoding`
when ImageMagick has hex-encoded the EXIF into a text chunk instead of writing
`eXIf`.

## Reading `has_gps`

Because only a prefix of each file is read, a positive and a negative answer
carry different weight:

- **`true` is always authoritative.** A validated latitude and longitude
  cannot be un-found by reading more bytes, so a position counts even when the
  EXIF block was cut off.
- **`false` is authoritative only when the scan completed** or the file has no
  EXIF at all. Under any other `scan_outcome`, `has_gps` is `unknown` rather
  than a confident no.
- **`ifd_only`** means a GPS IFD exists but has no usable position: typically a
  partially stripped file, a blanked `0/0` coordinate, or only one of the two
  coordinates.

A coordinate only counts if it is three well-formed rationals in range: up to
90 degrees of latitude or 180 of longitude, and minutes and seconds under 60.
This keeps a wrong value offset that lands on unrelated bytes from reading as a
position.

## Known limitations

`has_gps = false` is a lower bound. Location is not detected in:

- HEIF/HEIC, AVIF and JPEG XL, which report `unsupported_container`.
- XMP, MakerNotes, an Android Motion Photo's appended video, or an MPF
  secondary image.
- `GPSDestLatitude` and `GPSDestLongitude`, which record where the camera was
  pointed rather than where it stood. No shipping phone or camera is known to
  write them without also writing the capture position.
- Images handed over as pixels (`skipped_bitmap`).
- A folder dropped onto the page itself rather than onto a file input. The page
  walks that folder with `webkitGetAsEntry`, which bypasses these hooks.

`kamadak-exif` does not bound how much work a crafted EXIF block can cost:
repeated IFD pointers multiply the entries it creates, and entries that each
declare the whole block as their value are all decoded. Both need a block built
to repeat itself, which accidental corruption does not produce, so they are
accepted as known issues rather than guarded against. The parse runs off the
main thread in a content process on a bounded prefix. See the `kamadak-exif`
audit notes in `supply-chain/audits.toml`.
