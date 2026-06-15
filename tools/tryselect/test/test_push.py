import json
from contextlib import ExitStack
from unittest.mock import MagicMock, patch

import mozunit
import pytest
from tryselect import push


@pytest.mark.parametrize(
    "method,labels,params,routes,expected",
    (
        pytest.param(
            "fuzzy",
            ["task-foo", "task-bar"],
            None,
            None,
            {
                "parameters": {
                    "optimize_target_tasks": False,
                    "try_task_config": {
                        "env": {"TRY_SELECTOR": "fuzzy"},
                        "tasks": ["task-bar", "task-foo"],
                    },
                },
                "version": 2,
            },
            id="basic",
        ),
        pytest.param(
            "fuzzy",
            ["task-foo"],
            {"existing_tasks": {"task-foo": "123", "task-bar": "abc"}},
            None,
            {
                "parameters": {
                    "existing_tasks": {"task-bar": "abc"},
                    "optimize_target_tasks": False,
                    "try_task_config": {
                        "env": {"TRY_SELECTOR": "fuzzy"},
                        "tasks": ["task-foo"],
                    },
                },
                "version": 2,
            },
            id="existing_tasks",
        ),
        pytest.param(
            "fuzzy",
            ["task-" + str(i) for i in range(1001)],  # 1001 tasks, over threshold
            None,
            None,
            {
                "parameters": {
                    "optimize_target_tasks": False,
                    "try_task_config": {
                        "env": {"TRY_SELECTOR": "fuzzy"},
                        "priority": "lowest",
                        "tasks": sorted(["task-" + str(i) for i in range(1001)]),
                    },
                },
                "version": 2,
            },
            id="large_push_with_priority",
        ),
        pytest.param(
            "fuzzy",
            ["task-" + str(i) for i in range(500)],  # 500 tasks with rebuild=3
            {"try_task_config": {"rebuild": 3}},
            None,
            {
                "parameters": {
                    "optimize_target_tasks": False,
                    "try_task_config": {
                        "env": {"TRY_SELECTOR": "fuzzy"},
                        "priority": "lowest",
                        "rebuild": 3,
                        "tasks": sorted(["task-" + str(i) for i in range(500)]),
                    },
                },
                "version": 2,
            },
            id="large_push_with_rebuild",
        ),
        pytest.param(
            "fuzzy",
            ["task-" + str(i) for i in range(100)],  # Under threshold
            None,
            None,
            {
                "parameters": {
                    "optimize_target_tasks": False,
                    "try_task_config": {
                        "env": {"TRY_SELECTOR": "fuzzy"},
                        "tasks": sorted(["task-" + str(i) for i in range(100)]),
                    },
                },
                "version": 2,
            },
            id="small_push_no_priority",
        ),
        pytest.param(
            "fuzzy",
            [
                "task-" + str(i) for i in range(1001)
            ],  # Large push with existing priority
            {"try_task_config": {"priority": "low"}},
            None,
            {
                "parameters": {
                    "optimize_target_tasks": False,
                    "try_task_config": {
                        "env": {"TRY_SELECTOR": "fuzzy"},
                        "priority": "low",  # Should keep existing priority
                        "tasks": sorted(["task-" + str(i) for i in range(1001)]),
                    },
                },
                "version": 2,
            },
            id="large_push_existing_priority",
        ),
    ),
)
def test_generate_try_task_config(method, labels, params, routes, expected):
    # Simulate user responding "yes" to the large push prompt
    with patch("builtins.input", return_value="y"):
        assert (
            push.generate_try_task_config(method, labels, params=params, routes=routes)
            == expected
        )


def test_large_push_user_declines():
    """Test that when user declines large push warning, the system exits."""
    with patch("builtins.input", return_value="n"):
        with pytest.raises(SystemExit) as exc_info:
            push.generate_try_task_config(
                "fuzzy",
                ["task-" + str(i) for i in range(1001)],
            )
        assert exc_info.value.code == 1


def test_large_push_warning_message(capsys):
    """Test that the warning message is displayed for large pushes."""
    with patch("builtins.input", return_value="y"):
        push.generate_try_task_config(
            "fuzzy",
            ["task-" + str(i) for i in range(1001)],
        )
        captured = capsys.readouterr()
        assert "Your push would schedule at least 1001 tasks" in captured.out
        assert "lowest priority" in captured.out


def test_get_sys_argv():
    input_argv = [
        "./mach",
        "try",
        "fuzzy",
        "--full",
        "--artifact",
        "--push-to-vcs",
        "--query",
        "'android-hw !shippable !nofis",
        "--no-push",
    ]
    expected_string = './mach try fuzzy --full --artifact --push-to-vcs --query "\'android-hw !shippable !nofis" --no-push'
    assert push.get_sys_argv(input_argv) == expected_string


def test_get_sys_argv_2():
    input_argv = [
        "./mach",
        "try",
        "fuzzy",
        "--query",
        "'test-linux1804-64-qr/opt-mochitest-plain-",
        "--worker-override=t-linux-large=gecko-t/t-linux-2204-wayland-experimental",
        "--no-push",
    ]
    expected_string = './mach try fuzzy --query "\'test-linux1804-64-qr/opt-mochitest-plain-" --worker-override=t-linux-large=gecko-t/t-linux-2204-wayland-experimental --no-push'
    assert push.get_sys_argv(input_argv) == expected_string


@pytest.mark.parametrize(
    "url,push_to_vcs,expect_direct_push",
    [
        pytest.param(
            "https://example.com/fake-try-repo",
            False,
            True,
            id="non_hg_remote_https",
        ),
        pytest.param(
            "git@github.com:mozilla/fake-try.git",
            False,
            True,
            id="non_hg_remote_git",
        ),
        pytest.param(
            "https://hg.mozilla.org/other-repo",
            False,
            True,
            id="non_hg_remote_partial_match",
        ),
        pytest.param(
            "ssh://hg.mozilla.org/try",
            False,
            False,
            id="hg_remote_uses_lando",
        ),
        pytest.param(
            "ssh://hg.mozilla.org/try",
            True,
            True,
            id="push_to_vcs",
        ),
    ],
)
def test_push_to_try_routing(
    mock_push_to_lando_try,
    url,
    push_to_vcs,
    expect_direct_push,
):
    mock_vcs = MagicMock()
    mock_vcs.get_remote_url.return_value = url
    mock_vcs.branch = "feature-branch"

    mock_metrics = MagicMock()
    mock_metrics.mach_try.commit_prep.start = MagicMock()
    mock_metrics.mach_try.commit_prep.stop = MagicMock()

    with ExitStack() as stack:
        stack.enter_context(patch("tryselect.push.vcs", mock_vcs))
        stack.enter_context(patch("tryselect.push.MACH_TRY_REMOTE", url))
        mock_lando = stack.enter_context(mock_push_to_lando_try)
        stack.enter_context(patch("tryselect.push.check_working_directory"))
        stack.enter_context(
            patch(
                "tryselect.push.generate_try_task_config",
                return_value={"tasks": ["task1"]},
            )
        )
        stack.enter_context(
            patch("tryselect.push.push_to_git_backing", return_value="deadbeef")
        )
        stack.enter_context(patch("tryselect.push.write_task_config_history"))

        push._is_hg_try.cache_clear()

        push.push_to_try(
            "fuzzy",
            "try: test",
            mock_metrics,
            push_to_vcs=push_to_vcs,
            dry_run=False,
        )

        if expect_direct_push:
            mock_lando.assert_not_called()
            mock_vcs.push_to_try.assert_called_once()
        else:
            mock_lando.assert_called_once()
            mock_vcs.push_to_try.assert_not_called()


def test_push_to_git_backing_returns_git_push_sha():
    """For Hg repos, vcs.push() returns the translated git SHA; that SHA is returned."""
    mock_client = MagicMock()
    mock_client.get.return_value = {"secret": {"ssh_privkey": "fake-key\n"}}

    mock_vcs = MagicMock()
    mock_vcs.head_rev = "hgsha123"
    mock_vcs.push.return_value = "gitsha456"

    with ExitStack() as stack:
        stack.enter_context(patch("tryselect.push.vcs", mock_vcs))
        stack.enter_context(
            patch("tryselect.push.get_client", return_value=mock_client)
        )
        result = push.push_to_git_backing("try")

    assert result == "gitsha456"
    mock_client.get.assert_called_once_with(push.GIT_BACKING_SECRET)
    call_kwargs = mock_vcs.push.call_args.kwargs
    assert call_kwargs["ref"] == "hgsha123"
    assert call_kwargs["dest_branch"] == "try/hgsha123"
    assert call_kwargs["force"] is True
    assert "-o IdentitiesOnly=yes" in call_kwargs["env"]["GIT_SSH_COMMAND"]
    assert (
        "-o StrictHostKeyChecking=accept-new" in call_kwargs["env"]["GIT_SSH_COMMAND"]
    )


def test_push_to_git_backing_falls_back_to_head_rev():
    """For native git repos, vcs.push() returns None; vcs.head_rev is returned instead."""
    mock_client = MagicMock()
    mock_client.get.return_value = {"secret": {"ssh_privkey": "fake-key\n"}}

    mock_vcs = MagicMock()
    mock_vcs.head_rev = "gitsha789"
    mock_vcs.push.return_value = None

    with ExitStack() as stack:
        stack.enter_context(patch("tryselect.push.vcs", mock_vcs))
        stack.enter_context(
            patch("tryselect.push.get_client", return_value=mock_client)
        )
        result = push.push_to_git_backing("try")

    assert result == "gitsha789"


def test_push_to_try_injects_git_backing_params():
    """push_to_try injects head_git_repository and head_git_rev into try_task_config."""
    url = "ssh://hg.mozilla.org/try"
    mock_metrics = MagicMock()

    with ExitStack() as stack:
        stack.enter_context(patch("tryselect.push.MACH_TRY_REMOTE", url))
        stack.enter_context(patch("tryselect.push.check_working_directory"))
        stack.enter_context(patch("tryselect.push.write_task_config_history"))
        stack.enter_context(
            patch("tryselect.push.push_to_git_backing", return_value="deadbeef123")
        )
        push._is_hg_try.cache_clear()

        push.push_to_try(
            "fuzzy",
            "try: test",
            mock_metrics,
            try_task_config={
                "version": 2,
                "parameters": {"try_task_config": {"tasks": ["task1"]}},
            },
            push_to_vcs=True,
            dry_run=False,
        )

    call_kwargs = push.vcs.push_to_try.call_args.kwargs
    config = json.loads(call_kwargs["changed_files"]["try_task_config.json"])
    assert config["parameters"]["head_git_repository"] == push.GIT_BACKING_REPO
    assert config["parameters"]["head_git_rev"] == "deadbeef123"


if __name__ == "__main__":
    mozunit.main()
