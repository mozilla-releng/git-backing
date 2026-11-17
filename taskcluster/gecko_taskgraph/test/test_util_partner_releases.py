# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from copy import deepcopy

import pytest
from mozunit import main
from pytest_taskgraph import make_task

from gecko_taskgraph.test.conftest import FakeTransformConfig
from gecko_taskgraph.transforms import job as job_transforms  # noqa: F401
from gecko_taskgraph.transforms import task as task_transforms
from gecko_taskgraph.transforms.beetmover_push_to_release import (
    make_beetmover_push_to_release_description,
)
from gecko_taskgraph.util.partners import merge_partner_configs


def test_merge_partner_configs_preserves_unrelated_entries():
    first = {
        "release-partner-repack": {
            "acme": {"one": {"build_number": 1, "locales": ["en-US", "de"]}},
            "other": {"two": {"build_number": 2}},
        },
        "release-partner-attribution": {
            "defaults": {"source": "old", "medium": "distribution"},
            "configs": [{"campaign": "first", "content": "one", "build_number": 1}],
        },
    }
    second = {
        "release-partner-repack": {
            "acme": {"one": {"build_number": 8, "locales": ["en-US"]}},
            "new": {"three": {"build_number": 9}},
        },
        "release-partner-attribution": {
            "defaults": {"source": "new"},
            "configs": [{"campaign": "second", "content": "two", "build_number": 9}],
        },
    }
    original = deepcopy(first)
    result = merge_partner_configs([first, second])
    assert result["release-partner-repack"] == {
        "acme": {"one": {"build_number": 8, "locales": ["en-US"]}},
        "other": {"two": {"build_number": 2}},
        "new": {"three": {"build_number": 9}},
    }
    assert result["release-partner-attribution"]["configs"] == [
        {
            "campaign": "first",
            "content": "one",
            "build_number": 1,
            "source": "old",
            "medium": "distribution",
        },
        {"campaign": "second", "content": "two", "build_number": 9, "source": "new"},
    ]
    assert first == original
    from urllib.parse import parse_qs

    from gecko_taskgraph.util.partners import generate_attribution_code

    attribution = result["release-partner-attribution"]
    codes = [
        parse_qs(generate_attribution_code(attribution["defaults"], entry))
        for entry in attribution["configs"]
    ]
    assert codes[0]["source"] == ["old"]
    assert codes[0]["medium"] == ["distribution"]
    assert codes[1]["source"] == ["new"]


@pytest.fixture
def upload_config():
    checksums = make_task(
        label="checksums", kind="release-generate-checksums-beetmover"
    )
    return FakeTransformConfig(
        kind="release-beetmover-push-to-release",
        params={
            "target_tasks_method": "push_desktop",
            "release_partner_config": {
                "release-partner-repack": {
                    "acme": {
                        "one": {
                            "publish_to_releases": True,
                            "platforms": ["win32-shippable"],
                            "locales": ["en-US"],
                            "build_number": 7,
                            "upload_tasks": ["uploaded-partner"],
                        }
                    }
                }
            },
            "release_partners": [],
            "existing_tasks": {},
            "do_not_optimize": [],
        },
        kind_dependencies_tasks={"checksums": checksums},
    )


def test_push_uses_recorded_upload_ids(upload_config, mocker):
    mocker.patch.object(
        task_transforms,
        "get_release_config",
        return_value={"version": "160.0", "build_number": 2},
    )
    task = {
        "worker": {"product": "firefox"},
        "dependencies": {"checksums": "checksums"},
    }
    result = {}
    task_transforms.build_beetmover_push_to_release_payload(upload_config, task, result)
    assert task["dependencies"] == {"checksums": "checksums"}
    assert result["dependencies"] == ["uploaded-partner"]
    assert result["payload"]["partners"] == ["acme/one"]
    assert result["payload"]["partner_build_numbers"] == {"acme/one": 7}


@pytest.mark.parametrize("uploads", [None, [], "task", [None]])
def test_missing_recorded_uploads_are_rejected(upload_config, uploads):
    upload_config.params["release_partner_config"]["release-partner-repack"]["acme"][
        "one"
    ]["upload_tasks"] = uploads
    task = {
        "name": "firefox-push-to-release",
        "product": "firefox",
        "treeherder-platform": "firefox/opt",
        "dependencies": {},
    }
    with pytest.raises(ValueError, match="No supplied uploads for acme/one"):
        list(make_beetmover_push_to_release_description(upload_config, [task]))


@pytest.mark.parametrize(
    "state,flavor,version,passes",
    [
        ("completed", "push_firefox", "160.0", True),
        ("completed", "ship_firefox", "160.0", True),
        ("failed", "push_firefox", "160.0", False),
        ("running", "push_firefox", "160.0", False),
        ("completed", "push_firefox_partner", "160.0", False),
        ("completed", "push_firefox", "other", False),
    ],
)
def test_partner_push_requires_completed_regular_push(
    mocker, state, flavor, version, passes
):
    from gecko_taskgraph.util import partner_releases as releases

    params = {
        "project": "try",
        "head_rev": "abc",
        "version": "160.0",
        "build_number": 2,
    }
    definitions = {
        "supplied": {
            "extra": {
                "action": {
                    "name": "release-promotion",
                    "context": {"input": {"release_promotion_flavor": flavor}},
                }
            }
        },
        "push-task": {
            "payload": {"product": "firefox", "version": version, "build_number": 2}
        },
    }
    mocker.patch.object(
        releases, "get_task_definition", side_effect=definitions.__getitem__
    )
    mocker.patch.object(
        releases,
        "get_artifact",
        side_effect=lambda task, name: (
            params
            if name.endswith("parameters.yml")
            else {"firefox-push-to-release": "push-task"}
        ),
    )
    queue = mocker.Mock()
    queue.status.return_value = {"status": {"state": state}}
    mocker.patch.object(releases, "get_taskcluster_client", return_value=queue)
    if passes:
        assert (
            releases.require_completed_release_push(["supplied"], params) == "push-task"
        )
    else:
        with pytest.raises(ValueError, match="completed regular Firefox push"):
            releases.require_completed_release_push(["supplied"], params)


def test_partner_push_binds_all_existing_prerequisites(upload_config, mocker):
    upload_config.params.update(
        target_tasks_method="push_partner",
        existing_tasks={"checksums": "completed-checksums"},
    )
    mocker.patch.object(
        task_transforms,
        "get_release_config",
        return_value={"version": "160.0", "build_number": 2},
    )
    task = {
        "worker": {"product": "firefox"},
        "dependencies": {"checksums": "checksums"},
    }
    result = {}
    task_transforms.build_beetmover_push_to_release_payload(upload_config, task, result)
    assert task["dependencies"] == {}
    assert result["dependencies"] == ["uploaded-partner", "completed-checksums"]


def test_partner_push_rejects_rebuilding_release_prerequisites(upload_config, mocker):
    import gecko_taskgraph.transforms.beetmover_push_to_release as push

    mocker.patch.object(push, "get_beetmover_bucket_scope", return_value="bucket")
    mocker.patch.object(push, "add_scope_prefix", return_value="action")
    upload_config.params["target_tasks_method"] = "push_partner"
    task = {
        "name": "firefox-push-to-release",
        "product": "firefox",
        "treeherder-platform": "firefox/opt",
        "dependencies": {"checksums": "checksums"},
    }
    with pytest.raises(ValueError, match="must reuse completed prerequisite checksums"):
        list(make_beetmover_push_to_release_description(upload_config, [task]))


def test_promotion_does_not_require_already_uploaded_partners(upload_config, mocker):
    import gecko_taskgraph.transforms.beetmover_push_to_release as push

    mocker.patch.object(push, "get_beetmover_bucket_scope", return_value="bucket")
    mocker.patch.object(push, "add_scope_prefix", return_value="action")
    upload_config.params.update(
        target_tasks_method="promote_partner", existing_tasks={}
    )
    upload_config.params["release_partner_config"]["release-partner-repack"]["acme"][
        "one"
    ].pop("upload_tasks")
    task = {
        "name": "firefox-push-to-release",
        "product": "firefox",
        "treeherder-platform": "firefox/opt",
        "dependencies": {"checksums": "checksums"},
    }
    assert list(make_beetmover_push_to_release_description(upload_config, [task]))
    mocker.patch.object(
        task_transforms,
        "get_release_config",
        return_value={"version": "160.0", "build_number": 2},
    )
    definition = {
        "worker": {"product": "firefox"},
        "dependencies": {"checksums": "checksums"},
    }
    result = {}
    task_transforms.build_beetmover_push_to_release_payload(
        upload_config, definition, result
    )
    assert definition["dependencies"] == {"checksums": "checksums"}
    assert "dependencies" not in result


def test_uploads_with_colliding_labels_keep_distinct_producers(mocker):
    from gecko_taskgraph.util.partner_releases import (
        get_partner_upload_tasks,
        record_partner_upload_tasks,
    )

    configs = []
    for campaign, number, task_id in [
        ("first", 3, "first-upload"),
        ("second", 4, "second-upload"),
    ]:
        config = {
            "release-partner-attribution": {
                "defaults": {},
                "configs": [
                    {
                        "campaign": campaign,
                        "content": "one",
                        "build_number": number,
                        "publish_to_releases": True,
                        "platforms": ["win32-shippable"],
                    }
                ],
            }
        }
        graph = {
            "same-label": {
                "kind": "release-partner-attribution-beetmover",
                "task": {
                    "payload": {
                        "upstreamArtifacts": [
                            {
                                "locale": f"partner-repacks/{campaign}/one/v{number}/win32/en-US"
                            }
                        ]
                    }
                },
            }
        }
        record_partner_upload_tasks(config, graph, {"same-label": task_id})
        configs.append(config)
    config = FakeTransformConfig(
        params={
            "target_tasks_method": "push_desktop",
            "release_partner_config": merge_partner_configs(configs),
            "release_partners": [],
        }
    )
    assert get_partner_upload_tasks(config) == ["first-upload", "second-upload"]
    mocker.patch.object(
        task_transforms,
        "get_release_config",
        return_value={"version": "160.0", "build_number": 2},
    )
    result = {}
    task_transforms.build_beetmover_push_to_release_payload(
        config, {"worker": {"product": "firefox"}}, result
    )
    assert result["dependencies"] == ["first-upload", "second-upload"]
    assert result["payload"]["partner_build_numbers"] == {
        "first/one": 3,
        "second/one": 4,
    }


def test_unscheduled_source_upload_is_rejected():
    from gecko_taskgraph.util.partner_releases import record_partner_upload_tasks

    config = {
        "release-partner-repack": {
            "acme": {"one": {"build_number": 7, "publish_to_releases": True}}
        }
    }
    graph = {
        "missing": {
            "kind": "release-partner-repack-beetmover",
            "task": {
                "payload": {
                    "upstreamArtifacts": [
                        {"locale": "partner-repacks/acme/one/v7/win32/en-US"}
                    ]
                }
            },
        }
    }
    with pytest.raises(ValueError, match="was not scheduled"):
        record_partner_upload_tasks(config, graph, {})


if __name__ == "__main__":
    main()
