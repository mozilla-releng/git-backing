# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


from gecko_taskgraph.util.partners import (
    get_partner_config_entries,
    get_partner_config_subset,
)

PARTNER_BEETMOVER_KINDS = (
    "release-partner-repack-beetmover",
    "release-partner-attribution-beetmover",
)


def record_partner_upload_tasks(partner_config, graph, label_to_taskid):
    """Associate published config entries with the uploads actually scheduled by a graph."""
    entries = {
        (kind, partner, subpartner): entry
        for kind, partner, subpartner, entry in get_partner_config_entries(
            partner_config
        )
        if kind in ("release-partner-repack", "release-partner-attribution")
        and entry.get("publish_to_releases")
        and not entry.get("upload_tasks")
    }
    found = {key: set() for key in entries}
    for label, task in graph.items():
        if task["kind"] not in PARTNER_BEETMOVER_KINDS:
            continue
        for artifact in task["task"]["payload"].get("upstreamArtifacts", []):
            path = artifact.get("locale", "").split("/")
            if len(path) < 6 or path[0] != "partner-repacks":
                continue
            key = (task["kind"].removesuffix("-beetmover"), path[1], path[2])
            entry = entries.get(key)
            if entry is None or path[3] != f"v{entry['build_number']}":
                continue
            if label not in label_to_taskid:
                raise ValueError(
                    f"Partner upload {label} was not scheduled in its supplied graph"
                )
            found[key].add(label_to_taskid[label])
    for key, entry in entries.items():
        if not found[key]:
            raise ValueError(
                f"No supplied uploads for {key[1]}/{key[2]} build {entry['build_number']}"
            )
        entry["upload_tasks"] = sorted(found[key])


def get_partner_upload_tasks(config):
    task_ids = set()
    for kind, partner, subpartner, entry in get_partner_config_entries(
        get_partner_config_subset(
            config.params["release_partner_config"] or {},
            config.params.get("release_partners"),
        )
    ):
        if kind not in (
            "release-partner-repack",
            "release-partner-attribution",
        ) or not entry.get("publish_to_releases"):
            continue
        uploads = entry.get("upload_tasks")
        if (
            not isinstance(uploads, list)
            or not uploads
            or not all(isinstance(task_id, str) and task_id for task_id in uploads)
        ):
            raise ValueError(f"No supplied uploads for {partner}/{subpartner}")
        task_ids.update(uploads)
    return sorted(task_ids)


def is_partner_publication(parameters):
    return parameters.get("target_tasks_method") in (
        "push_desktop",
        "ship_desktop",
    )
