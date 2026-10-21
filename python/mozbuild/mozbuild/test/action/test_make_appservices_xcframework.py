# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import plistlib
import tarfile
import unittest
import zipfile
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import mock

import mozunit

from mozbuild.action import make_appservices_xcframework


class TestMakeAppservicesXcframework(unittest.TestCase):
    def setUp(self):
        self._tmpdir = TemporaryDirectory()
        self.tmpdir = Path(self._tmpdir.name)

    def tearDown(self):
        self._tmpdir.cleanup()

    def write_slice(self, name, binary, bindings):
        path = self.tmpdir / name
        path.mkdir()
        for megazord in ("FocusRustComponents", "MozillaRustComponents"):
            with zipfile.ZipFile(path / f"{megazord}.zip", "w") as z:
                framework = "MozillaRustComponents.framework"
                z.writestr(f"{framework}/MozillaRustComponents", f"{megazord}-{binary}")
                if bindings:
                    z.writestr(f"{framework}/Headers/loginsFFI.h", "header")
                    z.writestr(f"{framework}/Modules/module.modulemap", "modulemap")
                    z.writestr("Generated/logins.swift", f"{megazord}-swift")
                    z.writestr("Generated/Metrics/Metrics.swift", "metrics")
        return path

    def test_make_appservices_xcframework(self):
        output_dir = self.tmpdir / "out"
        commands = []

        def lipo(command):
            tool, create, arm64, x86_64, output, fat = command
            commands.append([
                tool,
                create,
                Path(arm64).name,
                Path(x86_64).name,
                output,
                Path(fat).name,
            ])
            Path(fat).write_bytes(
                Path(arm64).read_bytes() + b"+" + Path(x86_64).read_bytes()
            )

        with mock.patch.object(
            make_appservices_xcframework.subprocess, "check_call", side_effect=lipo
        ):
            self.assertEqual(
                make_appservices_xcframework.main([
                    "--device",
                    str(self.write_slice("device", "device-binary", True)),
                    "--simulator",
                    str(self.write_slice("simulator", "simulator-binary", False)),
                    "--simulator-x86_64",
                    str(self.write_slice("simulator-x86_64", "x86_64-binary", False)),
                    "--lipo",
                    "lipo",
                    "--output-dir",
                    str(output_dir),
                ]),
                0,
            )

        self.assertEqual(
            commands,
            [
                [
                    "lipo",
                    "-create",
                    "ios-arm64_x86_64-simulator-arm64",
                    "ios-arm64_x86_64-simulator-x86_64",
                    "-output",
                    "ios-arm64_x86_64-simulator",
                ]
            ]
            * 2,
        )

        with zipfile.ZipFile(output_dir / "MozillaRustComponents.xcframework.zip") as z:
            self.assertEqual(
                sorted(z.namelist()),
                [
                    "MozillaRustComponents.xcframework/Info.plist",
                    "MozillaRustComponents.xcframework/ios-arm64/MozillaRustComponents.framework/Headers/loginsFFI.h",
                    "MozillaRustComponents.xcframework/ios-arm64/MozillaRustComponents.framework/Modules/module.modulemap",
                    "MozillaRustComponents.xcframework/ios-arm64/MozillaRustComponents.framework/MozillaRustComponents",
                    "MozillaRustComponents.xcframework/ios-arm64_x86_64-simulator/MozillaRustComponents.framework/Headers/loginsFFI.h",
                    "MozillaRustComponents.xcframework/ios-arm64_x86_64-simulator/MozillaRustComponents.framework/Modules/module.modulemap",
                    "MozillaRustComponents.xcframework/ios-arm64_x86_64-simulator/MozillaRustComponents.framework/MozillaRustComponents",
                ],
            )
            self.assertEqual(
                z.read(
                    "MozillaRustComponents.xcframework/ios-arm64_x86_64-simulator/MozillaRustComponents.framework/MozillaRustComponents"
                ),
                b"MozillaRustComponents-simulator-binary+MozillaRustComponents-x86_64-binary",
            )
            self.assertEqual(
                plistlib.loads(z.read("MozillaRustComponents.xcframework/Info.plist")),
                {
                    "AvailableLibraries": [
                        {
                            "LibraryIdentifier": "ios-arm64",
                            "LibraryPath": "MozillaRustComponents.framework",
                            "SupportedArchitectures": ["arm64"],
                            "SupportedPlatform": "ios",
                        },
                        {
                            "LibraryIdentifier": "ios-arm64_x86_64-simulator",
                            "LibraryPath": "MozillaRustComponents.framework",
                            "SupportedArchitectures": ["arm64", "x86_64"],
                            "SupportedPlatform": "ios",
                            "SupportedPlatformVariant": "simulator",
                        },
                    ],
                    "CFBundlePackageType": "XFWK",
                    "XCFrameworkFormatVersion": "1.0",
                },
            )

        with tarfile.open(output_dir / "swift-components.tar.xz") as tar:
            self.assertEqual(
                sorted(tar.getnames()),
                [
                    "swift-components/all/Generated/Metrics/Metrics.swift",
                    "swift-components/all/Generated/logins.swift",
                    "swift-components/focus/Generated/Metrics/Metrics.swift",
                    "swift-components/focus/Generated/logins.swift",
                ],
            )
            self.assertEqual(
                tar.extractfile("swift-components/focus/Generated/logins.swift").read(),
                b"FocusRustComponents-swift",
            )

        with zipfile.ZipFile(output_dir / "FocusRustComponents.xcframework.zip") as z:
            self.assertEqual(
                z.read(
                    "FocusRustComponents.xcframework/ios-arm64/MozillaRustComponents.framework/MozillaRustComponents"
                ),
                b"FocusRustComponents-device-binary",
            )


if __name__ == "__main__":
    mozunit.main()
