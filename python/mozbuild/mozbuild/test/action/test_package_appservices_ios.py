# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import unittest
import zipfile
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import mock

import mozunit

from mozbuild.action import package_appservices_ios


class TestPackageAppservicesIos(unittest.TestCase):
    def test_generate_glean_metrics_failure(self):
        with TemporaryDirectory() as tmpdir:
            with mock.patch.object(
                package_appservices_ios.translate, "translate", return_value=1
            ):
                with self.assertRaisesRegex(
                    Exception, "glean_parser failed to generate the Swift metrics"
                ):
                    package_appservices_ios.generate_glean_metrics(
                        Path(tmpdir), ["components/nimbus/metrics.yaml"]
                    )

    def test_package_simulator(self):
        substs = {
            "IPHONEOS_IS_SIMULATOR": True,
            "HOST_BIN_SUFFIX": "",
            "LIB_PREFIX": "lib",
            "LIB_SUFFIX": "a",
            "RUST_TARGET": "aarch64-apple-ios-sim",
        }
        with TemporaryDirectory() as tmpdir:
            topobjdir = Path(tmpdir)
            libdir = topobjdir / "aarch64-apple-ios-sim" / "release-megazord"
            libdir.mkdir(parents=True)
            for crate in ("megazord_focus", "megazord_ios"):
                (libdir / f"lib{crate}.a").write_bytes(crate.encode())
            with mock.patch.object(
                package_appservices_ios.buildconfig, "substs", substs
            ):
                with mock.patch.object(
                    package_appservices_ios.buildconfig, "topobjdir", tmpdir
                ):
                    self.assertEqual(
                        package_appservices_ios.main([str(topobjdir / "out")]), 0
                    )

            for name, crate in (
                ("FocusRustComponents", "megazord_focus"),
                ("MozillaRustComponents", "megazord_ios"),
            ):
                with zipfile.ZipFile(topobjdir / "out" / f"{name}.zip") as z:
                    self.assertEqual(
                        z.namelist(),
                        ["MozillaRustComponents.framework/MozillaRustComponents"],
                    )
                    self.assertEqual(
                        z.read("MozillaRustComponents.framework/MozillaRustComponents"),
                        crate.encode(),
                    )

    def test_package_device(self):
        substs = {
            "CARGO": "cargo",
            "RUSTC": "rustc",
            "HOST_BIN_SUFFIX": "",
            "LIB_PREFIX": "lib",
            "LIB_SUFFIX": "a",
            "RUST_TARGET": "aarch64-apple-ios",
        }
        glean_inputs = []

        def bindgen(command, **kwargs):
            out_dir = Path(command[4])
            out_dir.mkdir(parents=True, exist_ok=True)
            if "--modulemap" in command:
                (out_dir / "module.modulemap").write_bytes(
                    b"framework module MozillaRustComponents {\n"
                    b'    header "fooFFI.h"\n'
                    b"}\n"
                )
                return
            (out_dir / "fooFFI.h").write_text("header")
            (out_dir / "foo.swift").write_text("swift")
            (out_dir / "megazord_ios.modulemap").write_text("modulemap")
            (out_dir / "glean_sym.swift").write_text("glean_sym")
            (out_dir / "glean_sym.h").write_text("glean_sym")

        def translate(yaml_files, language, output_dir, options):
            glean_inputs.append([f"{f.parent.name}/{f.name}" for f in yaml_files])
            output_dir.mkdir(parents=True)
            (output_dir / "Metrics.swift").write_text("metrics")
            return 0

        with TemporaryDirectory() as tmpdir:
            topobjdir = Path(tmpdir)
            libdir = topobjdir / "aarch64-apple-ios" / "release-megazord"
            libdir.mkdir(parents=True)
            for crate in ("megazord_focus", "megazord_ios"):
                (libdir / f"lib{crate}.a").write_bytes(crate.encode())
            with mock.patch.object(
                package_appservices_ios.buildconfig, "substs", substs
            ):
                with mock.patch.object(
                    package_appservices_ios.buildconfig, "topobjdir", tmpdir
                ):
                    with mock.patch.object(
                        package_appservices_ios.subprocess,
                        "check_call",
                        side_effect=bindgen,
                    ):
                        with mock.patch.object(
                            package_appservices_ios.translate,
                            "translate",
                            side_effect=translate,
                        ):
                            self.assertEqual(
                                package_appservices_ios.main([str(topobjdir / "out")]),
                                0,
                            )

            framework = "MozillaRustComponents.framework"
            common = [
                f"{framework}/Headers/MozillaRustComponents.h",
                f"{framework}/Headers/RustViaductFFI.h",
                f"{framework}/Headers/fooFFI.h",
                f"{framework}/Headers/glean_sym.h",
                f"{framework}/Modules/module.modulemap",
                f"{framework}/MozillaRustComponents",
                "Generated/Metrics/Metrics.swift",
                "Generated/foo.swift",
                "Generated/fooFFI.h",
                "Generated/megazord_ios.modulemap",
            ]
            for name in ("FocusRustComponents", "MozillaRustComponents"):
                with zipfile.ZipFile(topobjdir / "out" / f"{name}.zip") as z:
                    self.assertEqual(sorted(z.namelist()), sorted(common))
                    self.assertEqual(
                        z.read(f"{framework}/Modules/module.modulemap").decode(),
                        "framework module MozillaRustComponents {\n"
                        '    header "RustViaductFFI.h"\n'
                        '    header "fooFFI.h"\n'
                        "}\n",
                    )
            self.assertEqual(
                glean_inputs,
                [
                    [
                        "ads-client/metrics.yaml",
                        "nimbus/metrics.yaml",
                        "nimbus/pings.yaml",
                        "logins/metrics.yaml",
                        "sync_manager/metrics.yaml",
                        "sync_manager/pings.yaml",
                    ],
                    ["nimbus/metrics.yaml", "nimbus/pings.yaml"],
                ],
            )


if __name__ == "__main__":
    mozunit.main()
