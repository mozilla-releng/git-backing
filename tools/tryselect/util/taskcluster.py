# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import hashlib
import json
import os
import secrets
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlencode, urlparse

from mach.util import get_state_dir

TC_ROOT_URL = "https://firefox-ci-tc.services.mozilla.com"
TC_CREDENTIALS_FILE = (
    Path(get_state_dir(specific_to_topsrcdir=False)) / "tc_credentials.json"
)
_CREDENTIAL_LIFETIME_S = 60 * 60 * 24 * 30  # 30 days


def _scopes_key(scopes: list[str]) -> str:
    return hashlib.sha256(json.dumps(sorted(scopes)).encode()).hexdigest()[:16]


def _load_cached_credentials(scopes: list[str]) -> dict | None:
    if not TC_CREDENTIALS_FILE.exists():
        return None
    try:
        cache = json.loads(TC_CREDENTIALS_FILE.read_text())
        entry = cache.get(_scopes_key(scopes))
        if entry and entry.get("expires", 0) > time.time():
            return {"clientId": entry["clientId"], "accessToken": entry["accessToken"]}
    except (json.JSONDecodeError, KeyError):
        pass
    return None


def _save_credentials(clientId: str, accessToken: str, scopes: list[str]) -> None:
    TC_CREDENTIALS_FILE.parent.mkdir(parents=True, exist_ok=True)
    try:
        cache = json.loads(TC_CREDENTIALS_FILE.read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        cache = {}
    cache[_scopes_key(scopes)] = {
        "clientId": clientId,
        "accessToken": accessToken,
        "expires": time.time() + _CREDENTIAL_LIFETIME_S,
    }
    TC_CREDENTIALS_FILE.touch(mode=0o600, exist_ok=True)
    TC_CREDENTIALS_FILE.write_text(json.dumps(cache))


def _browser_auth(scopes: list[str]) -> dict:
    """Open the TC client-creation UI and wait for the callback."""
    credentials = {}

    class _Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            qs = parse_qs(urlparse(self.path).query)
            credentials["clientId"] = qs.get("clientId", [""])[0]
            credentials["accessToken"] = qs.get("accessToken", [""])[0]
            self.send_response(200)
            self.end_headers()
            self.wfile.write(
                b"<html><body><h1>Signed in to Taskcluster</h1>"
                b"<p>You may close this window.</p></body></html>"
            )

        def log_message(self, *args):
            pass

    server = HTTPServer(("127.0.0.1", 0), _Handler)
    port = server.server_address[1]
    callback_url = f"http://localhost:{port}"

    params = urlencode(
        {
            "scope": scopes,
            "name": f"mach-try-{secrets.token_hex(4)}",
            "expires": "1d",
            "callback_url": callback_url,
            "description": "Temporary client for mach try",
        },
        doseq=True,
    )
    login_url = f"{TC_ROOT_URL}/auth/clients/create?{params}"

    print(f"Opening browser for Taskcluster sign-in: {login_url}")
    webbrowser.open(login_url)

    server.handle_request()
    server.server_close()

    if not credentials.get("clientId") or not credentials.get("accessToken"):
        raise RuntimeError("Taskcluster sign-in did not return credentials.")

    _save_credentials(credentials["clientId"], credentials["accessToken"], scopes)
    return credentials


def get_taskcluster_credentials(scopes: list[str]) -> dict:
    """Return a dict with 'clientId' and 'accessToken' for Taskcluster.

    Checks environment variables first, then a cached credentials file, and
    finally falls back to the browser-redirect auth flow.

    `scopes` is passed to the TC UI so the created client has the right
    permissions (e.g. ['hooks:trigger-hook:git-push/mozilla/firefox-try/*']).
    Credentials are cached per distinct scope set.
    """
    if os.environ.get("TASKCLUSTER_CLIENT_ID") and os.environ.get(
        "TASKCLUSTER_ACCESS_TOKEN"
    ):
        return {
            "clientId": os.environ["TASKCLUSTER_CLIENT_ID"],
            "accessToken": os.environ["TASKCLUSTER_ACCESS_TOKEN"],
        }

    cached = _load_cached_credentials(scopes)
    if cached:
        return cached

    return _browser_auth(scopes)
