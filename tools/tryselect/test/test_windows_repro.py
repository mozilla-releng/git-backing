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

import mozunit
import pytest


def test_windows_repro(tmp_path):
    results = []
    keyfile = tmp_path / "repro.pem"

    with open(keyfile, "w") as fh:
        fh.write(
            "-----BEGIN OPENSSH PRIVATE KEY-----\n"
            "not a real key, just testing whether a locked file can be read\n"
            "-----END OPENSSH PRIVATE KEY-----\n"
        )
        fh.flush()
        os.chmod(keyfile, stat.S_IRUSR | stat.S_IWUSR)
        # File handle intentionally still open here -- this mirrors the old,
        # buggy push_to_git_backing() implementation before it was fixed.

        proc = subprocess.run(
            [sys.executable, "-c", f"print(open(r'{keyfile}').read())"],
            capture_output=True,
            text=True,
        )
        results.append(
            f"python reopen: rc={proc.returncode} "
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
                f"ssh-keygen -y: rc={proc.returncode} "
                f"stdout={proc.stdout!r} stderr={proc.stderr!r}"
            )
        else:
            results.append("ssh-keygen: not found on PATH")

    pytest.fail("\n".join(results))


if __name__ == "__main__":
    mozunit.main()
