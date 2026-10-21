# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import plistlib
import subprocess
import sys
import tarfile
from io import BytesIO
from pathlib import Path
from tempfile import TemporaryDirectory

from mozpack.mozjar import JarReader, JarWriter

FRAMEWORK = "MozillaRustComponents.framework"
BINARY = f"{FRAMEWORK}/MozillaRustComponents"
SWIFT_COMPONENTS = {
    "FocusRustComponents": "focus",
    "MozillaRustComponents": "all",
}


def make_xcframework(name, slices, output_dir, swift_components, lipo):
    libraries = []
    device = JarReader(str(Path(slices[0][1][0]) / f"{name}.zip"))
    names = [member.filename for member in device]
    if f"{FRAMEWORK}/Modules/module.modulemap" not in names or not any(
        n.startswith("Generated/") for n in names
    ):
        raise Exception(f"{name}.zip from the device build has no bindings")
    with JarWriter(
        file=str(output_dir / f"{name}.xcframework.zip")
    ) as xcframework, TemporaryDirectory() as tmpdir:
        for member in device:
            if member.filename.startswith("Generated/"):
                data = member.read()
                info = tarfile.TarInfo(
                    f"swift-components/{SWIFT_COMPONENTS[name]}/{member.filename}"
                )
                info.size = len(data)
                swift_components.addfile(info, BytesIO(data))
            elif member.filename != BINARY:
                for identifier, _, _, _ in slices:
                    xcframework.add(
                        f"{name}.xcframework/{identifier}/{member.filename}", member
                    )
        for identifier, archive_dirs, architectures, extra in slices:
            binaries = [
                JarReader(str(Path(archive_dir) / f"{name}.zip"))[BINARY]
                for archive_dir in archive_dirs
            ]
            if len(binaries) == 1:
                binary = binaries[0]
            else:
                thin = []
                for architecture, member in zip(architectures, binaries):
                    path = Path(tmpdir) / f"{identifier}-{architecture}"
                    path.write_bytes(member.read())
                    thin.append(str(path))
                fat = Path(tmpdir) / identifier
                subprocess.check_call([lipo, "-create", *thin, "-output", str(fat)])
                binary = fat.read_bytes()
            xcframework.add(f"{name}.xcframework/{identifier}/{BINARY}", binary)
            libraries.append({
                "LibraryIdentifier": identifier,
                "LibraryPath": FRAMEWORK,
                "SupportedArchitectures": architectures,
                "SupportedPlatform": "ios",
                **extra,
            })

        info_plist = BytesIO()
        plistlib.dump(
            {
                "AvailableLibraries": libraries,
                "CFBundlePackageType": "XFWK",
                "XCFrameworkFormatVersion": "1.0",
            },
            info_plist,
        )
        xcframework.add(f"{name}.xcframework/Info.plist", info_plist.getvalue())


def main(argv):
    parser = argparse.ArgumentParser(
        description="Combine the per target application-services iOS archives "
        "into XCFrameworks."
    )
    parser.add_argument(
        "--device",
        required=True,
        help="Directory holding the archives built for aarch64-apple-ios",
    )
    parser.add_argument(
        "--simulator",
        required=True,
        help="Directory holding the archives built for aarch64-apple-ios-sim",
    )
    parser.add_argument(
        "--simulator-x86_64",
        required=True,
        help="Directory holding the archives built for x86_64-apple-ios",
    )
    parser.add_argument(
        "--lipo",
        required=True,
        help="Path to the lipo tool that joins the two simulator archives",
    )
    parser.add_argument(
        "--output-dir",
        required=True,
        help="Directory to write the XCFramework and Swift sources archives to",
    )
    args = parser.parse_args(argv)

    slices = [
        ("ios-arm64", [args.device], ["arm64"], {}),
        (
            "ios-arm64_x86_64-simulator",
            [args.simulator, args.simulator_x86_64],
            ["arm64", "x86_64"],
            {"SupportedPlatformVariant": "simulator"},
        ),
    ]
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    try:
        with tarfile.open(output_dir / "swift-components.tar.xz", "w:xz") as tar:
            for name in SWIFT_COMPONENTS:
                make_xcframework(name, slices, output_dir, tar, args.lipo)
    except subprocess.CalledProcessError as e:
        return e.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
