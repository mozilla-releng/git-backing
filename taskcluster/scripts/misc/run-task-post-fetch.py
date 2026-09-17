# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Wire up fetched Python and uv toolchains before the task command runs.

Invoked in-process by run-task's ``RUN_TASK_PRE_COMMAND_HOOK`` (see
``taskgraph.run-task``), after checkout and fetches complete but before the
task command executes, so the PATH/env changes made here are visible to it.
Set by the ``use-python``/``use-uv`` job transforms; no-ops for whichever
toolchain wasn't fetched.
"""

import os
import platform
import subprocess

IS_WINDOWS = platform.system() == "Windows"
IS_MACOSX = platform.system() == "Darwin"


def setup_python():
    if "MOZ_PYTHON_HOME" not in os.environ:
        return

    print("Setting up local python environment")
    prev = [os.environ["PATH"]] if "PATH" in os.environ else []

    moz_python_home = os.environ["MOZ_PYTHON_HOME"]
    if IS_WINDOWS:
        ext = ".exe"
        moz_python_bindir = moz_python_home
    else:
        ext = ""
        moz_python_bindir = moz_python_home + "/bin"

    new_path = os.pathsep.join([moz_python_bindir] + prev)
    os.environ["PATH"] = new_path

    # Relocate the python binary. The standard way uses PYTHONHOME, but that
    # conflicts with system python (e.g. used by hg), so the toolchain
    # python is patched to use MOZPYTHONHOME instead.
    os.environ["MOZPYTHONHOME"] = moz_python_home

    pyinterp = os.path.join(moz_python_bindir, f"python3{ext}")
    if not os.path.exists(pyinterp):
        raise RuntimeError(
            "Inconsistent Python installation: archive found, but no "
            "python3 binary detected"
        )

    if IS_MACOSX:
        # We may not have access to the system certificates, so use the
        # certifi ones.
        certifi_cert_file = subprocess.check_output(
            [pyinterp, "-c", "import certifi; print(certifi.where())"],
            text=True,
        )
        os.environ["SSL_CERT_FILE"] = certifi_cert_file.strip()
        print("patching ssl certificate")

    print(f"updated PATH with python artifact: {new_path}")


def setup_uv():
    if "MOZ_UV_HOME" not in os.environ:
        return

    print("Adding uv to PATH")
    prev = [os.environ["PATH"]] if "PATH" in os.environ else []
    moz_uv_bindir = os.environ["MOZ_UV_HOME"]
    new_path = os.pathsep.join([moz_uv_bindir] + prev)
    os.environ["PATH"] = new_path
    print(f"updated PATH with uv artifact: {new_path}")


setup_python()
setup_uv()
