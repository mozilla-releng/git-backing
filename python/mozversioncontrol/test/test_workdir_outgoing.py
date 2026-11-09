# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import subprocess

import mozunit
import pytest

from mozversioncontrol import get_repository_object

STEPS = {
    "hg": [
        """
        echo "bar" >> bar
        echo "baz" > baz
        hg add baz
        hg rm foo
        """,
        """
        hg commit -m "Remove foo; modify bar; add baz"
        """,
        """
        echo ooka >> baz
        echo newborn > baby
        hg add baby
        """,
        """
        hg commit -m "Modify baz; add baby"
        """,
    ],
    "git": [
        """
        echo "bar" >> bar
        echo "baz" > baz
        git add baz
        git rm foo
        """,
        """
        git commit -am "Remove foo; modify bar; add baz"
        """,
        """
        echo ooka >> baz
        echo newborn > baby
        git add baz baby
        """,
        """
        git commit -m "Modify baz; add baby"
        """,
    ],
    "jj": [
        # snapshot, since bug 1962245 suppresses automatic ones
        """
        echo "bar" >> bar
        echo "baz" > baz
        rm foo
        jj log -n0
        """,
        """
        jj commit -m "Remove foo; modify bar; add baz"
        """,
        """
        echo ooka >> baz
        echo newborn > baby
        jj log -n0
        """,
        """
        jj describe -m "Modify baz; add baby"
        """,
    ],
}


def assert_files(actual, expected):
    assert set(map(os.path.basename, actual)) == set(expected)


def test_workdir_outgoing(repo):
    vcs = get_repository_object(repo.dir)
    assert vcs.path == str(repo.dir)

    remote_path = {
        "hg": "../remoterepo",
        "git": "upstream/master",
        "jj": "master@upstream",
    }[repo.vcs]

    # Mutate files.
    repo.execute_next_step()

    assert_files(vcs.get_changed_files("A", "all"), ["baz"])
    assert_files(vcs.get_changed_files("AM", "all"), ["bar", "baz"])
    assert_files(vcs.get_changed_files("D", "all"), ["foo"])
    if repo.vcs == "git":
        assert_files(vcs.get_changed_files("AM", mode="staged"), ["baz"])
    else:
        # Mercurial and jj do not use a staging area (and ignore the mode
        # parameter.)
        assert_files(vcs.get_changed_files("AM", "unstaged"), ["bar", "baz"])
    if repo.vcs != "jj":
        # Everything is already committed in jj, and therefore outgoing.
        assert_files(vcs.get_outgoing_files("AMD"), [])
        assert_files(vcs.get_outgoing_files("AMD", remote_path), [])

    # Create a commit.
    repo.execute_next_step()

    assert_files(vcs.get_changed_files("AMD", "all"), [])
    assert_files(vcs.get_changed_files("AMD", "staged"), [])
    assert_files(vcs.get_outgoing_files("AMD"), ["bar", "baz", "foo"])
    assert_files(vcs.get_outgoing_files("AMD", remote_path), ["bar", "baz", "foo"])

    # Mutate again.
    repo.execute_next_step()

    assert_files(vcs.get_changed_files("A", "all"), ["baby"])
    assert_files(vcs.get_changed_files("AM", "all"), ["baby", "baz"])
    assert_files(vcs.get_changed_files("D", "all"), [])

    # Create a second commit.
    repo.execute_next_step()

    assert_files(vcs.get_outgoing_files("AM"), ["bar", "baz", "baby"])
    assert_files(vcs.get_outgoing_files("AM", remote_path), ["bar", "baz", "baby"])
    if repo.vcs == "git":
        assert_files(vcs.get_changed_files("AM", rev="HEAD~1"), ["bar", "baz"])
        assert_files(vcs.get_changed_files("AM", rev="HEAD"), ["baby", "baz"])
    elif repo.vcs == "hg":
        assert_files(vcs.get_changed_files("AM", rev=".^"), ["bar", "baz"])
        assert_files(vcs.get_changed_files("AM", rev="."), ["baby", "baz"])
        assert_files(vcs.get_changed_files("AM", rev=".^::"), ["bar", "baz", "baby"])
        assert_files(vcs.get_changed_files("AM", rev="modifies(baz)"), ["baz", "baby"])
    elif repo.vcs == "jj":
        assert_files(vcs.get_changed_files("AM", rev="@-"), ["bar", "baz"])
        assert_files(vcs.get_changed_files("AM", rev="@"), ["baby", "baz"])
        assert_files(vcs.get_changed_files("AM", rev="@-::"), ["bar", "baz", "baby"])
        # Currently no direct equivalent of `modifies(baz)`. `files(baz)` will
        # also select changes that added or deleted baz, and the diff_filter
        # will applied be too late.
        assert_files(
            vcs.get_changed_files("AMD", rev="files(baz)"),
            ["foo", "baz", "baby", "bar"],
        )


def test_outgoing_files_shallow_clone(repo):
    """A shallow clone has no history, so the outgoing files are the diff
    against an explicit base revision, the way CI tasks get them."""
    if repo.vcs != "git":
        pytest.skip("shallow clones are a git feature")

    def git(*args, cwd):
        return subprocess.check_output(["git", *args], cwd=cwd, text=True).strip()

    for _ in range(4):
        repo.execute_next_step()
    base = git("rev-parse", "HEAD~2", cwd=repo.dir)
    head = git("rev-parse", "HEAD", cwd=repo.dir)

    git("config", "uploadpack.allowAnySHA1InWant", "true", cwd=repo.dir)
    shallow = repo.dir.parent / "shallow"
    git(
        "clone",
        "--quiet",
        "--depth=1",
        "--no-checkout",
        f"file://{repo.dir}",
        str(shallow),
        cwd=repo.dir.parent,
    )
    for rev in (base, head):
        git("fetch", "--quiet", "--depth=1", "origin", rev, cwd=shallow)
    git("checkout", "--quiet", head, cwd=shallow)

    vcs = get_repository_object(shallow)
    assert vcs.is_shallow
    assert_files(vcs.get_outgoing_files("AMD", base), ["bar", "baz", "foo", "baby"])
    assert_files(vcs.get_outgoing_files("A", base), ["baz", "baby"])
    assert_files(vcs.get_outgoing_files("D", base), ["foo"])


if __name__ == "__main__":
    mozunit.main()
