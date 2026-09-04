#!/usr/bin/env python3
"""Evaluate Android 14 probe evidence without authorizing an unknown build."""

from __future__ import annotations

import argparse
import pathlib
import sys
from typing import Dict, Iterable


KNOWN_EXACT_PROFILES = {
    "oneplus7pro-p202303230244": ("31", "per_app"),
    "nx769j-ukq1-20240417": ("34", "per_app"),
}


def read_properties(path: pathlib.Path) -> Dict[str, str]:
    values: Dict[str, str] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"{path}:{number}: expected key=value")
        key, value = line.split("=", 1)
        if not key or key in values:
            raise ValueError(f"{path}:{number}: invalid or duplicate key")
        values[key] = value
    return values


def require(values: Dict[str, str], keys: Iterable[str], source: str) -> None:
    missing = [key for key in keys if key not in values]
    if missing:
        raise ValueError(f"{source}: missing {','.join(missing)}")


def parse_mask(value: str, key: str) -> int:
    try:
        parsed = int(value, 0)
    except ValueError as error:
        raise ValueError(f"router stats: invalid {key}") from error
    if parsed < 0 or parsed > 0xFFFFFFFFFFFFFFFF:
        raise ValueError(f"router stats: out-of-range {key}")
    return parsed


def evaluate(
    profile: Dict[str, str],
    router: Dict[str, str] | None,
    topology: Dict[str, str] | None,
) -> Dict[str, str]:
    require(
        profile,
        (
            "schema_version",
            "sdk",
            "profile_id",
            "profile_status",
            "platform_family",
            "platform_candidate_status",
            "platform_candidate_reason",
            "recommended_route_scope",
            "activation_policy",
            "routing_authorized",
            "qualification_basis",
            "candidate_requirements",
        ),
        "device profile",
    )
    if profile["schema_version"] != "6":
        raise ValueError("device profile: schema_version 6 required")

    report = {
        "schema_version": "1",
        "profile_id": profile["profile_id"],
        "profile_status": profile["profile_status"],
        "platform_family": profile["platform_family"],
        "static_status": profile["platform_candidate_status"],
        "protocol_status": "not_run",
        "topology_status": "not_checked",
        "evidence_status": "blocked",
        "recommended_route_scope": profile["recommended_route_scope"],
        "activation_policy": "blocked",
        "routing_authorized": "false",
        "reason": profile["platform_candidate_reason"],
        "remaining_checks": profile["candidate_requirements"],
    }

    exact_profile = (
        profile["profile_id"] in KNOWN_EXACT_PROFILES
        and profile["profile_status"] == "qualified"
        and profile["platform_candidate_status"] == "qualified"
        and profile["activation_policy"] == "exact_profile"
        and profile["routing_authorized"] == "true"
        and profile["qualification_basis"] == "committed_recipe"
        and profile["sdk"] == KNOWN_EXACT_PROFILES[profile["profile_id"]][0]
        and profile["recommended_route_scope"]
        == KNOWN_EXACT_PROFILES[profile["profile_id"]][1]
    )
    if exact_profile:
        report.update(
            evidence_status="exact_profile",
            activation_policy="exact_profile",
            routing_authorized="true",
            reason="committed_recipe_verified",
            remaining_checks="none",
        )
        return report

    if profile["sdk"] != "34" or profile["platform_candidate_status"] != "probe_required":
        return report

    report.update(
        evidence_status="probe_required",
        activation_policy="probe_only",
        routing_authorized="false",
        reason="runtime_protocol_evidence_required",
    )
    if router is None:
        return report

    required_router_fields = (
        "state",
        "protocol_verdict",
        "protocol_required_mask",
        "protocol_seen_mask",
        "protocol_valid_mask",
        "protocol_invalid_mask",
        "protocol_unsupported_mask",
    )
    require(router, required_router_fields, "router stats")
    if router["state"] != "pass_through_ready":
        report.update(
            protocol_status="rejected",
            evidence_status="rejected",
            reason="generic_probe_must_remain_pass_through",
        )
        return report

    masks = {
        key: parse_mask(router[key], key)
        for key in required_router_fields
        if key.startswith("protocol_") and key.endswith("_mask")
    }
    required = masks["protocol_required_mask"]
    protocol_valid = (
        required != 0
        and router["protocol_verdict"] == "probe_compatible"
        and masks["protocol_seen_mask"] & required == required
        and masks["protocol_valid_mask"] & required == required
        and masks["protocol_invalid_mask"] & required == 0
        and masks["protocol_unsupported_mask"] & required == 0
    )
    if not protocol_valid:
        report.update(
            protocol_status="rejected",
            evidence_status="rejected",
            reason="required_protocol_roles_not_validated",
        )
        return report

    report.update(
        protocol_status="probe_compatible",
        evidence_status="topology_required",
        reason="validated_topology_required",
    )
    if topology is None:
        return report

    required_topology_fields = (
        "schema",
        "back_camera2_id",
        "back_camera1_index",
        "front_camera2_id",
        "front_camera1_index",
        "internal_back_camera1_index",
        "internal_front_camera1_index",
    )
    require(topology, required_topology_fields, "topology")
    numeric_fields = (
        "back_camera1_index",
        "internal_back_camera1_index",
        "internal_front_camera1_index",
    )
    topology_valid = (
        topology["schema"] == "1"
        and topology["back_camera2_id"] not in ("", "none")
        and all(topology[key].isdigit() for key in numeric_fields)
        and topology["internal_back_camera1_index"]
        != topology["internal_front_camera1_index"]
        and (
            topology["front_camera1_index"] == "none"
            or topology["front_camera1_index"].isdigit()
        )
        and (topology["front_camera2_id"] == "none")
        == (topology["front_camera1_index"] == "none")
    )
    if not topology_valid:
        report.update(
            topology_status="rejected",
            evidence_status="rejected",
            reason="invalid_camera_topology",
        )
        return report

    report.update(
        topology_status="validated",
        evidence_status="manual_review_required",
        activation_policy="manual_promotion_required",
        routing_authorized="false",
        reason="runtime_evidence_cannot_self_authorize",
        remaining_checks="global_preview,reboot_recovery,reviewed_recipe",
    )
    return report


def emit(report: Dict[str, str]) -> str:
    order = (
        "schema_version",
        "profile_id",
        "profile_status",
        "platform_family",
        "static_status",
        "protocol_status",
        "topology_status",
        "evidence_status",
        "recommended_route_scope",
        "activation_policy",
        "routing_authorized",
        "reason",
        "remaining_checks",
    )
    return "".join(f"{key}={report[key]}\n" for key in order)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("profile", type=pathlib.Path)
    parser.add_argument("--router-stats", type=pathlib.Path)
    parser.add_argument("--topology", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    try:
        report = evaluate(
            read_properties(args.profile),
            read_properties(args.router_stats) if args.router_stats else None,
            read_properties(args.topology) if args.topology else None,
        )
    except (OSError, ValueError) as error:
        print(f"capability evaluation failed: {error}", file=sys.stderr)
        return 2
    rendered = emit(report)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
