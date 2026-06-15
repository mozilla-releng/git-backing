# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import stat
import tempfile

import taskcluster
from tryselect.util.taskcluster import TC_ROOT_URL, get_taskcluster_credentials

GIT_BACKING_REPO = "https://github.com/mozilla-releng/git-backing"
GIT_BACKING_SSH = "git@github.com:mozilla-releng/git-backing.git"
BACKING_SECRET = "project/releng/releng-github-git-backing-ssh"


def _get_backing_ssh_key() -> str:

    creds = get_taskcluster_credentials([f"secrets:get:{BACKING_SECRET}"])
    secrets_client = taskcluster.Secrets({"rootUrl": TC_ROOT_URL, "credentials": creds})
    secret = secrets_client.get(BACKING_SECRET)
    return secret["secret"]["ssh_privkey"]


def _ssh_command(keyfile: str) -> str:
    return f"ssh -i '{keyfile}' -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new"


def push_to_git_backing(prefix: str) -> str:
    """Push the current head to the git-backing repo and return the git SHA."""
    from tryselect.push import vcs

    key = _get_backing_ssh_key()
    with tempfile.NamedTemporaryFile(mode="w", suffix=".pem", delete=False) as f:
        f.write(key)
        keyfile = f.name
    try:
        os.chmod(keyfile, stat.S_IRUSR | stat.S_IWUSR)
        ssh_cmd = _ssh_command(keyfile)

        if vcs.name in ("git", "jj"):
            sha = vcs.head_rev
            ref = f"refs/heads/{prefix}/{sha}"
            vcs.push(
                GIT_BACKING_SSH,
                ref=f"{sha}:{ref}",
                force=True,
                env={"GIT_SSH_COMMAND": ssh_cmd},
            )
            return sha
        else:
            raise RuntimeError(f"Unsupported VCS type: {vcs.name}")
    finally:
        os.unlink(keyfile)

