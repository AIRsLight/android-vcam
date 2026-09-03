#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

run_case() {
    name=$1
    fixture=$2
    expected_targets=$3
    expected_camera1_targets=$4
    output="$work/$name"
    mkdir -p "$output"
    sh "$root/portable-module/camera-map.sh" "$root/tests/fixtures/$fixture" "$output"
    diff -u <(printf '%b' "$expected_targets") "$output/targets.tsv"
    diff -u <(printf '%b' "$expected_camera1_targets") "$output/camera1-targets.tsv"
    grep -q $'^1000\t' "$output/camera1-map.tsv"
    grep -q $'^1001\t' "$output/camera1-map.tsv"
}

run_case avd camera-dump-aosp14-avd-vcam.txt \
    '10\t0\n' '0\t0\n'
run_case swapped camera-dump-two-physical-vcam.txt \
    '1\t0\n0\t1\n' '0\t0\n1\t1\n'

invalid_output="$work/invalid"
mkdir -p "$invalid_output"
printf 'preserve\n' > "$invalid_output/targets.tsv"
if sh "$root/portable-module/camera-map.sh" \
        "$root/tests/fixtures/camera-dump-invalid-duplicate.txt" \
        "$invalid_output" >/dev/null 2>&1; then
    echo "invalid topology was accepted" >&2
    exit 1
fi
diff -u <(printf 'preserve\n') "$invalid_output/targets.tsv"

echo "Camera map tests passed"
