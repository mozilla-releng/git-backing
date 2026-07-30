# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# THROWAWAY test to reproduce bug 1543241 on real Windows CI hardware. This is
# not a real regression test -- delete this file once we have a diagnosis.

import os
import shutil
import stat
import subprocess
import sys
import tempfile

import mozunit
import pytest


def _probe(keyfile, results, label):
    proc = subprocess.run(
        [sys.executable, "-c", f"print(open(r'{keyfile}').read())"],
        capture_output=True,
        text=True,
    )
    results.append(
        f"[{label}] python reopen: rc={proc.returncode} "
        f"stdout={proc.stdout!r} stderr={proc.stderr!r}"
    )

    ssh_keygen = shutil.which("ssh-keygen")
    if ssh_keygen:
        proc = subprocess.run(
            [ssh_keygen, "-y", "-f", str(keyfile)],
            capture_output=True,
            text=True,
        )
        results.append(
            f"[{label}] ssh-keygen -y: rc={proc.returncode} "
            f"stdout={proc.stdout!r} stderr={proc.stderr!r}"
        )
    else:
        results.append(f"[{label}] ssh-keygen: not found on PATH")


KEY_CONTENT = (
    "-----BEGIN OPENSSH PRIVATE KEY-----\n"
    "not a real key, just testing whether a locked file can be read\n"
    "-----END OPENSSH PRIVATE KEY-----\n"
)


def test_windows_repro(tmp_path):
    results = []

    # Variant 1: plain open(), like a naive temp file.
    plain_path = tmp_path / "plain.pem"
    with open(plain_path, "w") as fh:
        fh.write(KEY_CONTENT)
        fh.flush()
        os.chmod(plain_path, stat.S_IRUSR | stat.S_IWUSR)
        # File handle intentionally still open here.
        _probe(plain_path, results, "plain-open")

    # Variant 2: tempfile.NamedTemporaryFile(), exactly like the original
    # buggy push_to_git_backing() implementation before it was fixed.
    with tempfile.NamedTemporaryFile(mode="w", suffix=".pem") as keyfile:
        keyfile.write(KEY_CONTENT)
        os.chmod(keyfile.name, stat.S_IRUSR | stat.S_IWUSR)
        keyfile.flush()
        # File handle intentionally still open here -- this is the exact
        # pattern from the original bug.
        _probe(keyfile.name, results, "NamedTemporaryFile")

    pytest.fail("\n".join(results))


if __name__ == "__main__":
    mozunit.main()
