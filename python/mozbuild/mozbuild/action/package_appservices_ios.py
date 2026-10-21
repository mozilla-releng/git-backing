# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import buildconfig
from glean_parser import translate
from mozpack.mozjar import JarWriter

APPSERVICES = "third_party/application-services"
IOS_RUST = f"{APPSERVICES}/megazords/ios-rust"
FRAMEWORK = "MozillaRustComponents.framework"
MEGAZORDS = {
    "MozillaRustComponents": (
        IOS_RUST,
        [
            "components/ads-client/metrics.yaml",
            "components/nimbus/metrics.yaml",
            "components/nimbus/pings.yaml",
            "components/logins/metrics.yaml",
            "components/sync_manager/metrics.yaml",
            "components/sync_manager/pings.yaml",
        ],
    ),
    "FocusRustComponents": (
        f"{IOS_RUST}/focus",
        [
            "components/nimbus/metrics.yaml",
            "components/nimbus/pings.yaml",
        ],
    ),
}


def external_symbols(archive):
    defined, undefined = set(), set()
    output = subprocess.check_output(
        [buildconfig.substs["LLVM_OBJDUMP"], "-t", str(archive)], text=True
    )
    for line in output.splitlines():
        fields = line.split()
        if "*UND*" in fields:
            undefined.add(fields[-1])
        elif len(fields) > 1 and fields[1] in ("g", "w"):
            defined.add(fields[-1])
    return defined, undefined


def nss_symbols(nss_library):
    defined, undefined = external_symbols(nss_library)
    if not defined:
        raise Exception(f"Found no defined symbols in {nss_library.name}")
    return defined | {s for s in undefined if s.startswith("_sqlite3_")}


def check_nss_is_bundled(library, symbols):
    defined, undefined = external_symbols(library)
    if missing := sorted((undefined & symbols) - defined):
        raise Exception(
            f"{library.name} does not bundle NSS and its sqlite: {len(missing)} "
            f"of their symbols are undefined, starting with {missing[0]}"
        )


def generate_bindings(stage, bindgen, library, megazord_dir):
    substs = buildconfig.substs
    env = dict(os.environ)
    env["CARGO"] = substs["CARGO"]
    env["RUSTC"] = substs["RUSTC"]
    env["CARGO_HOME"] = str(Path(buildconfig.topobjdir) / ".cargo")
    env["CARGO_NET_OFFLINE"] = "true"

    def run_bindgen(out_dir, *flags):
        subprocess.check_call(
            [bindgen, "-l", library, "swift", out_dir, *flags],
            cwd=buildconfig.topsrcdir,
            env=env,
        )

    generated = stage / "Generated"
    run_bindgen(generated)
    headers = stage / FRAMEWORK / "Headers"
    headers.mkdir(parents=True)
    for header in generated.glob("*.h"):
        shutil.copyfile(header, headers / header.name)
    (generated / "glean_sym.swift").unlink(missing_ok=True)
    run_bindgen(
        stage / FRAMEWORK / "Modules",
        "--modulemap",
        "--xcframework",
        "--modulemap-filename",
        "module.modulemap",
    )

    srcdir = Path(buildconfig.topsrcdir)
    sources = [
        srcdir / megazord_dir / "MozillaRustComponents.h",
        srcdir
        / IOS_RUST
        / "Sources/MozillaRustComponentsWrapper/Viaduct/RustViaductFFI.h",
    ]
    for header in sources:
        shutil.copyfile(header, headers / header.name)

    modulemap = stage / FRAMEWORK / "Modules" / "module.modulemap"
    first, rest = modulemap.read_text(encoding="utf-8").split("\n", 1)
    with open(modulemap, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(f'{first}\n    header "RustViaductFFI.h"\n{rest}')
    components = srcdir / APPSERVICES / "components"
    return sources + sorted(components.rglob("uniffi.toml"))


def generate_glean_metrics(stage, yaml_files):
    srcdir = Path(buildconfig.topsrcdir) / APPSERVICES
    sources = [srcdir / f for f in yaml_files]
    if translate.translate(
        sources,
        "swift",
        stage / "Generated" / "Metrics",
        {"glean_namespace": "Glean", "build_date": "0"},
    ):
        raise Exception("glean_parser failed to generate the Swift metrics")
    return sources


def main(output, library, nss_library, bindgen=None):
    """Package an application-services iOS megazord, its headers, module map
    and generated Swift sources for one target."""
    megazord_dir, yaml_files = MEGAZORDS[Path(output.name).stem]
    library = Path(library).absolute()
    nss_library = Path(nss_library).absolute()
    check_nss_is_bundled(library, nss_symbols(nss_library))

    deps = set()
    with tempfile.TemporaryDirectory() as tmpdir:
        stage = Path(tmpdir)
        if bindgen:
            bindgen = Path(bindgen).absolute()
            deps.update(generate_bindings(stage, bindgen, library, megazord_dir))
            deps.update(generate_glean_metrics(stage, yaml_files))
        with JarWriter(fileobj=output) as jar:
            jar.add(f"{FRAMEWORK}/MozillaRustComponents", library.read_bytes())
            for path in sorted(p for p in stage.rglob("*") if p.is_file()):
                jar.add(path.relative_to(stage).as_posix(), path.read_bytes())
    return {dep.as_posix() for dep in deps}
