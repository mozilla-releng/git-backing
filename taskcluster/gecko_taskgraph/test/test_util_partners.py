# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import pytest
from mozunit import main
from taskgraph.task import Task

from gecko_taskgraph.test.conftest import FakeTransformConfig
from gecko_taskgraph.util.partners import get_partner_treeherder


@pytest.fixture
def dep_job():
    return Task(
        kind="release-partner-repack",
        label="release-partner-repack-win32-shippable",
        attributes={"build_platform": "win32-shippable"},
        task={
            "extra": {
                "treeherder-platform": "win32/opt",
                "treeherder": {
                    "tier": 1,
                    "machine": {"platform": "win32"},
                    "collection": {"opt": True},
                },
            }
        },
    )


def test_partner_symbols_distinguish_long_repack_ids(dep_job):
    partners = {
        partner: {
            "long-subpartner-name": {
                "platforms": ["win32-shippable"],
                "locales": ["en-US"],
            }
        }
        for partner in ["second-long-partner-name", "first-long-partner-name"]
    }
    config = FakeTransformConfig(
        kind="release-partner-repack-repackage-signing",
        params={
            "release_partners": None,
            "release_partner_config": {"release-partner-repack": partners},
        },
    )
    symbols = []
    for partner in sorted(partners):
        job = {"extra": {"repack_id": f"{partner}/long-subpartner-name/en-US"}}
        treeherder = get_partner_treeherder(config, job, dep_job, "rs")
        assert treeherder["platform"] == "win32/opt"
        assert treeherder["tier"] == 1
        symbols.append(treeherder["symbol"])
    assert symbols == ["rs(P-1-en-US)", "rs(P-2-en-US)"]


@pytest.mark.parametrize(
    "kind,extra,dep_extra,stage,expected",
    [
        (
            "release-partner-repack-mac-signing",
            {"repack_suffix": "2"},
            {},
            "s",
            "Rel(P-s-2)",
        ),
        (
            "release-partner-repack-mac-notarization",
            {},
            {"repack_suffix": "2"},
            "n",
            "Rel(P-n-2)",
        ),
        (
            "release-eme-free-repack-mac-signing",
            {"repack_suffix": "2"},
            {},
            "s",
            "Rel(EME-s-2)",
        ),
        (
            "release-eme-free-repack-beetmover-checksums",
            {},
            {"repack_id": "mozilla-EME-free/mozilla-EME-free/ja-JP-mac"},
            "cs",
            "Rel(EME-cs-ja-JP-mac)",
        ),
    ],
)
def test_partner_chunk_and_emefree_symbols(
    dep_job, kind, extra, dep_extra, stage, expected
):
    config = FakeTransformConfig(kind=kind)
    dep_job.task["extra"].update(dep_extra)
    assert (
        get_partner_treeherder(config, {"extra": extra}, dep_job, "Rel", stage)[
            "symbol"
        ]
        == expected
    )


if __name__ == "__main__":
    main()
