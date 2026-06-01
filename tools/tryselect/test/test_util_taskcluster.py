# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import time
from unittest.mock import patch

import mozunit
import pytest
from tryselect.util.taskcluster import (
    _load_cached_credentials,
    _save_credentials,
    get_taskcluster_credentials,
)


@pytest.fixture
def credentials_file(tmp_path, monkeypatch):
    creds_path = tmp_path / "tc_credentials.json"
    monkeypatch.setattr("tryselect.util.taskcluster.TC_CREDENTIALS_FILE", creds_path)
    return creds_path


SCOPES = ["some:scope"]


def _make_cache(credentials_file, scopes, expires_offset):
    from tryselect.util.taskcluster import _scopes_key

    credentials_file.write_text(
        json.dumps({
            _scopes_key(scopes): {
                "clientId": "cached-client",
                "accessToken": "cached-token",
                "expires": time.time() + expires_offset,
            }
        })
    )


@pytest.mark.parametrize(
    "content,expected",
    [
        pytest.param(None, None, id="missing"),
        pytest.param("not-json{{{", None, id="corrupt"),
        pytest.param(-1, None, id="expired"),
        pytest.param(3600, {"clientId": "test-client", "accessToken": "test-token"}, id="valid"),
    ],
)
def test_load_cached_credentials(credentials_file, content, expected):
    from tryselect.util.taskcluster import _scopes_key

    if content is None:
        pass
    elif isinstance(content, str):
        credentials_file.write_text(content)
    else:
        credentials_file.write_text(
            json.dumps({
                _scopes_key(SCOPES): {
                    "clientId": "test-client",
                    "accessToken": "test-token",
                    "expires": time.time() + content,
                }
            })
        )
    assert _load_cached_credentials(SCOPES) == expected


def test_load_cached_credentials_wrong_scopes(credentials_file):
    _make_cache(credentials_file, SCOPES, 3600)
    assert _load_cached_credentials(["other:scope"]) is None


def test_save_credentials(credentials_file):
    _save_credentials("my-client", "my-token", SCOPES)
    cache = json.loads(credentials_file.read_text())
    from tryselect.util.taskcluster import _scopes_key

    entry = cache[_scopes_key(SCOPES)]
    assert entry["clientId"] == "my-client"
    assert entry["accessToken"] == "my-token"
    assert entry["expires"] > time.time()


def test_save_credentials_distinct_scopes(credentials_file):
    from tryselect.util.taskcluster import _scopes_key

    scopes_a = ["scope:a"]
    scopes_b = ["scope:b"]
    _save_credentials("client-a", "token-a", scopes_a)
    _save_credentials("client-b", "token-b", scopes_b)
    cache = json.loads(credentials_file.read_text())
    assert cache[_scopes_key(scopes_a)]["clientId"] == "client-a"
    assert cache[_scopes_key(scopes_b)]["clientId"] == "client-b"


@pytest.mark.parametrize(
    "env_vars,cache_expires_offset,expect_browser_auth",
    [
        pytest.param(
            {"TASKCLUSTER_CLIENT_ID": "env-client", "TASKCLUSTER_ACCESS_TOKEN": "env-token"},
            None,
            False,
            id="from_env",
        ),
        pytest.param({}, 3600, False, id="from_cache"),
        pytest.param({}, None, True, id="browser_auth"),
    ],
)
def test_get_taskcluster_credentials(
    credentials_file, monkeypatch, env_vars, cache_expires_offset, expect_browser_auth
):
    monkeypatch.delenv("TASKCLUSTER_CLIENT_ID", raising=False)
    monkeypatch.delenv("TASKCLUSTER_ACCESS_TOKEN", raising=False)
    for key, val in env_vars.items():
        monkeypatch.setenv(key, val)

    if cache_expires_offset is not None:
        _make_cache(credentials_file, SCOPES, cache_expires_offset)

    browser_creds = {"clientId": "browser-client", "accessToken": "browser-token"}
    with patch(
        "tryselect.util.taskcluster._browser_auth", return_value=browser_creds
    ) as mock_auth:
        result = get_taskcluster_credentials(SCOPES)

    if env_vars:
        assert result == {"clientId": "env-client", "accessToken": "env-token"}
    elif cache_expires_offset is not None:
        assert result == {"clientId": "cached-client", "accessToken": "cached-token"}
    else:
        assert result == browser_creds

    if expect_browser_auth:
        mock_auth.assert_called_once_with(SCOPES)
    else:
        mock_auth.assert_not_called()


if __name__ == "__main__":
    mozunit.main()
