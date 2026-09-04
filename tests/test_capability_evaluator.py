from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
EVALUATOR = ROOT / "tools" / "evaluate-aosp14-capability.py"


def properties(**values: str) -> str:
    return "".join(f"{key}={value}\n" for key, value in values.items())


BASE_PROFILE = {
    "schema_version": "6",
    "sdk": "34",
    "profile_id": "none",
    "profile_status": "unsupported",
    "platform_family": "android14-camera-service",
    "platform_candidate_status": "probe_required",
    "platform_candidate_reason": "requires_non_authorizing_runtime_qualification",
    "recommended_route_scope": "global_only",
    "activation_policy": "probe_only",
    "routing_authorized": "false",
    "qualification_basis": "runtime_probe_required",
    "candidate_requirements": (
        "enforcing_provider_registration,pass_through_protocol,topology_maps,"
        "global_preview,reboot_recovery"
    ),
}

GOOD_ROUTER = {
    "state": "pass_through_ready",
    "protocol_verdict": "probe_compatible",
    "protocol_required_mask": "0x0000000000000f6f",
    "protocol_seen_mask": "0x0000000000000f6f",
    "protocol_valid_mask": "0x0000000000000f6f",
    "protocol_invalid_mask": "0x0",
    "protocol_unsupported_mask": "0x0",
}

GOOD_TOPOLOGY = {
    "schema": "1",
    "back_camera2_id": "10",
    "back_camera1_index": "0",
    "front_camera2_id": "none",
    "front_camera1_index": "none",
    "internal_back_camera1_index": "1",
    "internal_front_camera1_index": "2",
}


class CapabilityEvaluatorTest(unittest.TestCase):
    def run_evaluator(self, profile, router=None, topology=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            profile_path = root / "profile.conf"
            profile_path.write_text(properties(**profile), encoding="utf-8")
            command = [sys.executable, str(EVALUATOR), str(profile_path)]
            if router is not None:
                router_path = root / "router.stats"
                router_path.write_text(properties(**router), encoding="utf-8")
                command += ["--router-stats", str(router_path)]
            if topology is not None:
                topology_path = root / "topology.conf"
                topology_path.write_text(properties(**topology), encoding="utf-8")
                command += ["--topology", str(topology_path)]
            completed = subprocess.run(
                command, check=True, text=True, capture_output=True
            )
            return dict(
                line.split("=", 1)
                for line in completed.stdout.splitlines()
                if line
            )

    def test_probe_candidate_never_self_authorizes(self):
        result = self.run_evaluator(BASE_PROFILE, GOOD_ROUTER, GOOD_TOPOLOGY)
        self.assertEqual("manual_review_required", result["evidence_status"])
        self.assertEqual("manual_promotion_required", result["activation_policy"])
        self.assertEqual("false", result["routing_authorized"])
        self.assertEqual("global_only", result["recommended_route_scope"])

    def test_probe_starts_without_runtime_evidence(self):
        result = self.run_evaluator(BASE_PROFILE)
        self.assertEqual("probe_required", result["evidence_status"])
        self.assertEqual("not_run", result["protocol_status"])
        self.assertEqual("false", result["routing_authorized"])

    def test_generic_probe_rejects_active_routing(self):
        router = dict(GOOD_ROUTER, state="physical_route_ready")
        result = self.run_evaluator(BASE_PROFILE, router)
        self.assertEqual("rejected", result["evidence_status"])
        self.assertEqual("generic_probe_must_remain_pass_through", result["reason"])

    def test_missing_required_protocol_role_is_rejected(self):
        router = dict(GOOD_ROUTER, protocol_valid_mask="0x000000000000006f")
        result = self.run_evaluator(BASE_PROFILE, router)
        self.assertEqual("rejected", result["protocol_status"])
        self.assertEqual("false", result["routing_authorized"])

    def test_exact_committed_profile_remains_authorized(self):
        profile = dict(
            BASE_PROFILE,
            profile_id="nx769j-ukq1-20240417",
            profile_status="qualified",
            platform_family="exact-device-profile",
            platform_candidate_status="qualified",
            platform_candidate_reason="exact_profile_and_camera_abi_verified",
            recommended_route_scope="per_app",
            activation_policy="exact_profile",
            routing_authorized="true",
            qualification_basis="committed_recipe",
            candidate_requirements="none",
        )
        result = self.run_evaluator(profile)
        self.assertEqual("exact_profile", result["evidence_status"])
        self.assertEqual("true", result["routing_authorized"])

    def test_unlisted_profile_cannot_claim_exact_authorization(self):
        profile = dict(
            BASE_PROFILE,
            profile_id="self-declared-profile",
            profile_status="qualified",
            platform_family="exact-device-profile",
            platform_candidate_status="qualified",
            platform_candidate_reason="exact_profile_and_camera_abi_verified",
            recommended_route_scope="per_app",
            activation_policy="exact_profile",
            routing_authorized="true",
            qualification_basis="committed_recipe",
            candidate_requirements="none",
        )
        result = self.run_evaluator(profile)
        self.assertEqual("blocked", result["evidence_status"])
        self.assertEqual("false", result["routing_authorized"])

    def test_blocked_static_candidate_stays_blocked(self):
        profile = dict(
            BASE_PROFILE,
            platform_candidate_status="blocked",
            platform_candidate_reason="selinux_enforcing_required",
            activation_policy="blocked",
            qualification_basis="none",
        )
        result = self.run_evaluator(profile, GOOD_ROUTER, GOOD_TOPOLOGY)
        self.assertEqual("blocked", result["evidence_status"])
        self.assertEqual("selinux_enforcing_required", result["reason"])


if __name__ == "__main__":
    unittest.main()
