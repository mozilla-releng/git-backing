# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from copy import deepcopy

import pytest
from mozunit import main
from taskgraph.task import Task

from gecko_taskgraph.test.conftest import FakeTransformConfig
from gecko_taskgraph.transforms import job as job_transforms  # noqa: F401
from gecko_taskgraph.util.partners import (
    fix_partner_config,
    get_partner_build_numbers,
    get_partner_config_entries,
    get_partner_config_from_parameters,
    get_partner_treeherder,
)


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


@pytest.fixture
def partner_config():
    return {
        "release-partner-repack": {
            "acme": {
                "one": {
                    "locales": ["en-US"],
                    "platforms": ["linux64-shippable"],
                    "publish_to_releases": True,
                }
            },
            "other": {
                "two": {
                    "locales": ["en-US"],
                    "platforms": ["win32-shippable"],
                    "publish_to_releases": True,
                    "build_number": 4,
                }
            },
        },
        "release-partner-attribution": {
            "defaults": {},
            "configs": [
                {
                    "campaign": "acme",
                    "content": "attribution",
                    "locales": ["en-US"],
                    "platforms": ["win32-shippable"],
                    "publish_to_releases": True,
                    "build_number": 7,
                }
            ],
        },
    }


def test_partner_config_build_numbers(partner_config):
    original = deepcopy(partner_config)
    normalized = fix_partner_config(partner_config)
    config = FakeTransformConfig(
        params={"release_partner_config": normalized, "release_partners": []}
    )
    assert get_partner_build_numbers(config) == {
        "acme/one": 1,
        "other/two": 4,
        "acme/attribution": 7,
    }
    config.params["release_partners"] = ["acme"]
    assert get_partner_build_numbers(config) == {"acme/one": 1, "acme/attribution": 7}
    assert partner_config == original
    assert "other" in normalized["release-partner-repack"]


@pytest.mark.parametrize("number", [0, -1, True, 1.5, "2"])
def test_invalid_partner_build_number(partner_config, number):
    partner_config["release-partner-repack"]["acme"]["one"]["build_number"] = number
    with pytest.raises(ValueError, match="Invalid partner build number for acme/one"):
        fix_partner_config(partner_config)


def test_saved_legacy_partner_build_number(partner_config):
    original = deepcopy(partner_config)
    resolved = get_partner_config_from_parameters({
        "release_partner_config": partner_config,
        "release_partner_build_number": 3,
    })
    assert resolved["release-partner-repack"]["acme"]["one"]["build_number"] == 3
    assert resolved["release-partner-repack"]["other"]["two"]["build_number"] == 4
    assert partner_config == original


def test_attribution_uploads_use_each_partner_build_number():
    from gecko_taskgraph.transforms.partner_attribution_beetmover import (
        generate_upstream_artifacts,
    )

    artifacts = [
        ("path-one", "acme", "one", "win32", "en-US"),
        ("path-two", "other", "two", "mac", "ja-JP-mac"),
    ]
    result = generate_upstream_artifacts(
        "release-partner-attribution",
        artifacts,
        "partner-repacks/{partner}/{subpartner}/v{partner_build_number}/{platform}/{locale}",
        {("acme", "one"): 3, ("other", "two"): 8},
    )
    assert [item["locale"] for item in result] == [
        "partner-repacks/acme/one/v3/win32/en-US",
        "partner-repacks/other/two/v8/mac/ja-JP-mac",
    ]


def test_push_build_numbers_are_derived_from_config(partner_config, mocker):
    from gecko_taskgraph.transforms import task

    mocker.patch.object(
        task, "get_release_config", return_value={"version": "160.0", "build_number": 2}
    )
    config = FakeTransformConfig(
        params={
            "release_partners": [],
            "release_partner_config": fix_partner_config(partner_config),
        }
    )
    result = {}
    task.build_beetmover_push_to_release_payload(
        config, {"worker": {"product": "firefox"}}, result
    )
    assert result["payload"] == {
        "product": "firefox",
        "version": "160.0",
        "build_number": 2,
        "partners": ["acme/one", "other/two", "acme/attribution"],
        "partner_build_numbers": {"other/two": 4, "acme/attribution": 7},
    }
    for _, _, _, entry in get_partner_config_entries(
        config.params["release_partner_config"]
    ):
        entry["build_number"] = 1
    result = {}
    task.build_beetmover_push_to_release_payload(
        config, {"worker": {"product": "firefox"}}, result
    )
    assert "partner_build_numbers" not in result["payload"]


def test_repack_upload_uses_subpartner_build_number(partner_config, mocker):
    from gecko_taskgraph.transforms import beetmover_repackage_partner as beetmover

    mocker.patch.object(beetmover, "craft_release_properties", return_value={})
    config = FakeTransformConfig(
        kind="release-partner-repack-beetmover",
        params={
            "release_partner_config": fix_partner_config(partner_config),
            "release_partners": [],
            "build_number": 2,
            "version": "160.0",
        },
    )
    task = {
        "attributes": {
            "build_platform": "linux64-shippable",
            "artifact_prefix": "releng/partner",
        },
        "extra": {"repack_id": "other/two/en-US"},
        "dependencies": {"build": "build", "repackage-signing": "signing"},
        "partner-path": "partner-repacks/{partner}/{subpartner}/v{partner_build_number}/{platform}/{locale}",
    }
    result = list(beetmover.make_task_worker(config, [task]))[0]
    assert {item["locale"] for item in result["worker"]["upstream-artifacts"]} == {
        "partner-repacks/other/two/v4/linux-x86_64/en-US"
    }


if __name__ == "__main__":
    main()
