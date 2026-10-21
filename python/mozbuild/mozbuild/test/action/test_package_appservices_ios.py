# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import unittest
import zipfile
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import mock

import buildconfig
import mozunit

from mozbuild.action import package_appservices_ios

NSS_SYMBOL = "0000000000000010 g     F __TEXT,__text _NSS_NoDB_Init\n"
MEGAZORDS = (
    ("MozillaRustComponents", "megazord_ios"),
    ("FocusRustComponents", "megazord_focus"),
)


class TestPackageAppservicesIos(unittest.TestCase):
    def package(self, tmpdir, name, crate, *bindgen):
        (Path(tmpdir) / f"lib{crate}.a").write_bytes(crate.encode())
        archive = Path(tmpdir) / f"{name}.zip"
        cwd = os.getcwd()
        os.chdir(tmpdir)
        try:
            with open(archive, "wb") as output:
                deps = package_appservices_ios.main(
                    output, f"lib{crate}.a", "libnss_rustlib.a", *bindgen
                )
        finally:
            os.chdir(cwd)
        return archive, deps

    def test_package_no_nss_symbols(self):
        with TemporaryDirectory() as tmpdir, mock.patch.object(
            package_appservices_ios.buildconfig,
            "substs",
            {"LLVM_OBJDUMP": "llvm-objdump"},
        ), mock.patch.object(
            package_appservices_ios.subprocess, "check_output", return_value=""
        ):
            with self.assertRaisesRegex(
                Exception, "Found no defined symbols in libnss_rustlib.a"
            ):
                self.package(tmpdir, "MozillaRustComponents", "megazord_ios")

    def test_check_nss_is_bundled_failure(self):
        symbols = {
            "libnss_rustlib.a": "0000000000000010 g     F __TEXT,__text _NSS_NoDB_Init\n"
            "0000000000000020 g     F __TEXT,__text _PK11_AEADOp\n"
            "0000000000000030  w    F __TEXT,__text _PR_GetError\n"
            "0000000000000040 g     F __TEXT,__text _SECITEM_FreeItem\n"
            "0000000000000000         *UND* _malloc\n"
            "0000000000000000         *UND* _sqlite3_open\n",
            "libmegazord_focus.a": "0000000000000000 g     F __TEXT,__text _megazord\n"
            "0000000000000010 g     F __TEXT,__text _SECITEM_FreeItem\n"
            "0000000000000000         *UND* _NSS_NoDB_Init\n"
            "0000000000000000         *UND* _PK11_AEADOp\n"
            "0000000000000000         *UND* _PR_GetError\n"
            "0000000000000000         *UND* _SECITEM_FreeItem\n"
            "0000000000000000         *UND* _malloc\n"
            "0000000000000000         *UND* _sqlite3_open\n",
        }

        def objdump(command, **kwargs):
            return symbols[Path(command[2]).name]

        with mock.patch.object(
            package_appservices_ios.buildconfig,
            "substs",
            {"LLVM_OBJDUMP": "llvm-objdump"},
        ):
            with mock.patch.object(
                package_appservices_ios.subprocess, "check_output", side_effect=objdump
            ):
                nss_symbols = package_appservices_ios.nss_symbols(
                    Path("libnss_rustlib.a")
                )
                with self.assertRaisesRegex(
                    Exception,
                    "libmegazord_focus.a does not bundle NSS and its sqlite: 4 of "
                    "their symbols are undefined, starting with _NSS_NoDB_Init",
                ):
                    package_appservices_ios.check_nss_is_bundled(
                        Path("libmegazord_focus.a"), nss_symbols
                    )

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
        with TemporaryDirectory() as tmpdir, mock.patch.object(
            package_appservices_ios.buildconfig,
            "substs",
            {"LLVM_OBJDUMP": "llvm-objdump"},
        ), mock.patch.object(
            package_appservices_ios.subprocess, "check_output", return_value=NSS_SYMBOL
        ):
            for name, crate in MEGAZORDS:
                archive, deps = self.package(tmpdir, name, crate)
                self.assertEqual(deps, set())
                with zipfile.ZipFile(archive) as z:
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
            "LLVM_OBJDUMP": "llvm-objdump",
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
            (out_dir / "glean_symFFI.h").write_text("glean_sym")

        def translate(yaml_files, language, output_dir, options):
            glean_inputs.append([f"{f.parent.name}/{f.name}" for f in yaml_files])
            output_dir.mkdir(parents=True)
            (output_dir / "Metrics.swift").write_text("metrics")
            return 0

        framework = "MozillaRustComponents.framework"
        ios_rust = "third_party/application-services/megazords/ios-rust"
        components = "third_party/application-services/components"
        viaduct = (
            f"{ios_rust}/Sources/MozillaRustComponentsWrapper/Viaduct/RustViaductFFI.h"
        )
        expected_deps = {
            "MozillaRustComponents": [
                f"{components}/ads-client/metrics.yaml",
                f"{components}/logins/metrics.yaml",
                f"{components}/nimbus/metrics.yaml",
                f"{components}/nimbus/pings.yaml",
                f"{components}/sync_manager/metrics.yaml",
                f"{components}/sync_manager/pings.yaml",
                f"{ios_rust}/MozillaRustComponents.h",
                viaduct,
            ],
            "FocusRustComponents": [
                f"{components}/nimbus/metrics.yaml",
                f"{components}/nimbus/pings.yaml",
                viaduct,
                f"{ios_rust}/focus/MozillaRustComponents.h",
            ],
        }

        with TemporaryDirectory() as tmpdir, mock.patch.object(
            package_appservices_ios.buildconfig, "substs", substs
        ), mock.patch.object(
            package_appservices_ios.buildconfig, "topobjdir", tmpdir
        ), mock.patch.object(
            package_appservices_ios.subprocess, "check_call", side_effect=bindgen
        ) as check_call, mock.patch.object(
            package_appservices_ios.subprocess, "check_output", return_value=NSS_SYMBOL
        ), mock.patch.object(
            package_appservices_ios.translate, "translate", side_effect=translate
        ):
            for name, crate in MEGAZORDS:
                archive, deps = self.package(
                    tmpdir, name, crate, "host/bin/uniffi-bindgen-library-mode"
                )
                deps = [
                    Path(dep).relative_to(buildconfig.topsrcdir).as_posix()
                    for dep in deps
                ]
                self.assertIn(f"{components}/nimbus/uniffi.toml", deps)
                self.assertEqual(
                    sorted(d for d in deps if not d.endswith("/uniffi.toml")),
                    sorted(expected_deps[name]),
                )
                with zipfile.ZipFile(archive) as z:
                    self.assertEqual(
                        sorted(z.namelist()),
                        sorted([
                            f"{framework}/Headers/MozillaRustComponents.h",
                            f"{framework}/Headers/RustViaductFFI.h",
                            f"{framework}/Headers/fooFFI.h",
                            f"{framework}/Headers/glean_symFFI.h",
                            f"{framework}/Modules/module.modulemap",
                            f"{framework}/MozillaRustComponents",
                            "Generated/Metrics/Metrics.swift",
                            "Generated/foo.swift",
                            "Generated/fooFFI.h",
                            "Generated/glean_symFFI.h",
                            "Generated/megazord_ios.modulemap",
                        ]),
                    )
                    self.assertEqual(
                        z.read(f"{framework}/Modules/module.modulemap").decode(),
                        "framework module MozillaRustComponents {\n"
                        '    header "RustViaductFFI.h"\n'
                        '    header "fooFFI.h"\n'
                        "}\n",
                    )

            for call in check_call.call_args_list:
                self.assertEqual(
                    [call.args[0][i].is_absolute() for i in (0, 2, 4)],
                    [True, True, True],
                )
            self.assertEqual(
                [
                    [
                        c.args[0][0].name,
                        c.args[0][1],
                        c.args[0][2].name,
                        c.args[0][3],
                        c.args[0][4].name,
                        *c.args[0][5:],
                    ]
                    for c in check_call.call_args_list
                ],
                [
                    [
                        "uniffi-bindgen-library-mode",
                        "-l",
                        f"lib{crate}.a",
                        "swift",
                        out_dir,
                        *flags,
                    ]
                    for _, crate in MEGAZORDS
                    for out_dir, flags in (
                        ("Generated", []),
                        (
                            "Modules",
                            [
                                "--modulemap",
                                "--xcframework",
                                "--modulemap-filename",
                                "module.modulemap",
                            ],
                        ),
                    )
                ],
            )
            for call in check_call.call_args_list:
                self.assertEqual(
                    {
                        k: call.kwargs["env"][k]
                        for k in ("CARGO", "RUSTC", "CARGO_HOME", "CARGO_NET_OFFLINE")
                    },
                    {
                        "CARGO": "cargo",
                        "RUSTC": "rustc",
                        "CARGO_HOME": str(Path(tmpdir) / ".cargo"),
                        "CARGO_NET_OFFLINE": "true",
                    },
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
