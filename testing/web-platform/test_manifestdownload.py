# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from unittest import mock

import manifestdownload
import mozunit
import requests

OLD = "a" * 40
NEW = "b" * 40


def index_url(revision, name):
    return manifestdownload.TC_URL.format(changeset=revision, name=name)


def git_taskcluster_url(commits, found=None, error=None):
    requested = []

    def get(logger, url, **kwargs):
        requested.append(url)
        if error:
            raise error
        return mock.Mock(status_code=200 if url == found else 404)

    with mock.patch.object(manifestdownload, "get", get):
        url = manifestdownload.git_taskcluster_url(mock.Mock(), commits)
    return url, requested


def test_git_taskcluster_url_first_indexed_ancestor():
    found = index_url(OLD, "autoland")
    url, requested = git_taskcluster_url([NEW, OLD], found=found)
    assert url == found + manifestdownload.ARTIFACT_PATH
    assert requested == [
        index_url(NEW, name) for name in manifestdownload.REPOS.values()
    ] + [index_url(OLD, "mozilla-central"), found]


def test_git_taskcluster_url_not_found():
    url, requested = git_taskcluster_url([NEW, OLD])
    assert url == manifestdownload.DEFAULT_URL
    assert len(requested) == 2 * len(manifestdownload.REPOS)


def test_git_taskcluster_url_request_error():
    url, requested = git_taskcluster_url(
        [NEW, OLD], error=requests.exceptions.ConnectionError()
    )
    assert url == manifestdownload.DEFAULT_URL
    assert requested == [index_url(NEW, "mozilla-central")]


if __name__ == "__main__":
    mozunit.main()
