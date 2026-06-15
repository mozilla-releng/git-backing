# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from contextlib import ExitStack
from unittest.mock import MagicMock, patch

import mozunit
import pytest

# mach_commands registers commands against a global Registrar at import time;
# the "ci" category must exist first.
from mach.registrar import Registrar

if "ci" not in Registrar.categories:
    Registrar.register_category("ci", "CI", "CI-related tasks")

from tryselect import mach_commands  # noqa: E402


@pytest.fixture
def mock_context():
    return MagicMock()


def test_push_unknown_alias_exits(mock_context):
    mock_context._mach_context.parser.error.side_effect = SystemExit(2)
    with pytest.raises(SystemExit) as exc_info:
        mach_commands.push(mock_context, "unknown-branch")
    assert exc_info.value.code == 2
    mock_context._mach_context.parser.error.assert_called_once()
    assert "unknown-branch" in mock_context._mach_context.parser.error.call_args[0][0]


@pytest.mark.parametrize(
    "vcs_name,has_cinnabar,remote,expect_exit,expected_call",
    [
        pytest.param(
            "git",
            True,
            "ssh://hg.mozilla.org/projects/elm",
            False,
            (
                "hg::ssh://hg.mozilla.org/projects/elm",
                "deadbeef:refs/heads/branches/default/tip",
            ),
            id="git_hg_remote_with_cinnabar",
        ),
        pytest.param(
            "jj",
            True,
            "ssh://hg.mozilla.org/projects/elm",
            False,
            (
                "hg::ssh://hg.mozilla.org/projects/elm",
                "deadbeef:refs/heads/branches/default/tip",
            ),
            id="jj_hg_remote_with_cinnabar",
        ),
        pytest.param(
            "git",
            False,
            "ssh://hg.mozilla.org/projects/elm",
            True,
            None,
            id="git_hg_remote_no_cinnabar_exits",
        ),
        pytest.param(
            "git",
            False,
            "https://github.com/mozilla/gecko-dev",
            False,
            ("https://github.com/mozilla/gecko-dev", None),
            id="git_github_remote_no_cinnabar",
        ),
        pytest.param(
            "hg",
            None,
            "ssh://hg.mozilla.org/projects/elm",
            False,
            None,
            id="hg_direct_push",
        ),
        pytest.param(
            "git",
            True,
            "elm",
            False,
            (
                "hg::ssh://hg.mozilla.org/projects/elm",
                "deadbeef:refs/heads/branches/default/tip",
            ),
            id="git_alias_resolved",
        ),
    ],
)
def test_push_vcs_dispatch(
    mock_context, vcs_name, has_cinnabar, remote, expect_exit, expected_call
):
    mock_vcs = MagicMock()
    mock_vcs.name = vcs_name
    mock_vcs.has_git_cinnabar = has_cinnabar
    mock_vcs.head_rev = "deadbeef"

    with ExitStack() as stack:
        stack.enter_context(patch("tryselect.push.check_working_directory"))
        mock_backing = stack.enter_context(
            patch("tryselect.push.push_to_git_backing", return_value="sha")
        )
        stack.enter_context(patch("tryselect.push.vcs", mock_vcs))

        if expect_exit:
            with pytest.raises(SystemExit) as exc_info:
                mach_commands.push(mock_context, remote)
            assert exc_info.value.code == 1
            mock_backing.assert_not_called()
        else:
            mach_commands.push(mock_context, remote)
            if expected_call:
                dest, ref = expected_call
                if ref:
                    mock_vcs.push.assert_called_once_with(dest, ref=ref)
                else:
                    mock_vcs.push.assert_called_once_with(dest)
            else:
                mock_vcs.push.assert_called_once_with(remote)


if __name__ == "__main__":
    mozunit.main()
