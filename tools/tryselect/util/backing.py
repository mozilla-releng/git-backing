# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import stat
import subprocess
import sys
import tempfile

import taskcluster
from tryselect.util.taskcluster import TC_ROOT_URL, get_taskcluster_credentials

GIT_BACKING_REPO = "https://github.com/mozilla-releng/git-backing"
GIT_BACKING_SSH = "git@github.com:mozilla-releng/git-backing.git"
BACKING_SECRET = "project/releng/releng-github-git-backing-ssh"

HG_GIT_NOT_FOUND = """
Could not detect `hg-git`.

Pushing to the git-backing repo from a Mercurial checkout requires the hg-git
extension. Please install it:

    https://hg-git.github.io/
""".lstrip()

GIT_CINNABAR_NOT_FOUND = """
Could not detect `git-cinnabar`.

The `mach push-project` command requires git-cinnabar to be installed when
pushing from git. Please install it by running:

    $ ./mach vcs-setup
""".lstrip()


def _get_backing_ssh_key() -> str:
    creds = get_taskcluster_credentials([f"secrets:get:{BACKING_SECRET}"])
    secrets_client = taskcluster.Secrets({"rootUrl": TC_ROOT_URL, "credentials": creds})
    secret = secrets_client.get(BACKING_SECRET)
    return secret["secret"]["ssh_privkey"]


def _ssh_command(keyfile: str) -> str:
    return f"ssh -i '{keyfile}' -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new"


def _push_hg_to_backing(vcs, ssh_cmd: str) -> str:
    hg = str(vcs._tool)
    cwd = vcs.path

    result = subprocess.run(
        [hg, "config", "extensions.hggit"],
        cwd=cwd,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0 or not result.stdout.strip():
        print(HG_GIT_NOT_FOUND)
        sys.exit(1)

    # hg-git requires git+ssh:// URL format rather than the SCP git@ format
    # git@github.com:org/repo.git -> git+ssh://git@github.com/org/repo.git
    host, path = GIT_BACKING_SSH[4:].split(":", 1)
    hg_git_url = f"git+ssh://git@{host}/{path}"

    bookmark = f"machtry-{uuid.uuid4()}"
    env = {**os.environ, "GIT_SSH_COMMAND": ssh_cmd}
    subprocess.check_call([hg, "bookmark", bookmark], cwd=cwd)
    try:
        subprocess.check_call([hg, "push", "-B", bookmark, hg_git_url], cwd=cwd, env=env)
        sha = subprocess.check_output(
            [hg, "log", "-r", ".", "-T", "{gitnode}"], cwd=cwd, text=True
        ).strip()
        if not sha:
            raise RuntimeError(
                "hg-git did not set {gitnode} on the current revision after push"
            )
        return sha
    finally:
        subprocess.call([hg, "--quiet", "bookmark", "--delete", bookmark], cwd=cwd)


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
        elif vcs.name == "hg":
            return _push_hg_to_backing(vcs, ssh_cmd)
        else:
            raise RuntimeError(f"Unsupported VCS type: {vcs.name}")
    finally:
        os.unlink(keyfile)


def push_to_project_branch(vcs, project: str) -> None:
    """Push the current head to the given hg.mozilla.org project branch."""
    if vcs.name in ("git", "jj"):
        if not vcs.has_git_cinnabar:
            print(GIT_CINNABAR_NOT_FOUND)
            sys.exit(1)
        sha = vcs.head_rev
        vcs.push(
            f"hg::ssh://hg.mozilla.org/projects/{project}",
            ref=f"{sha}:refs/heads/branches/default/tip",
        )
    elif vcs.name == "hg":
        vcs.push(f"ssh://hg.mozilla.org/projects/{project}")
    else:
        raise RuntimeError(f"Unsupported VCS type: {vcs.name}")
