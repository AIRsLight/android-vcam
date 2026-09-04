#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 OTA_ZIP OUTPUT_DIR [PAYLOAD_DUMPER]" >&2
    exit 2
}

[[ $# -ge 2 && $# -le 3 ]] || usage

ota_zip=$(realpath "$1")
output_dir=$(realpath -m "$2")
payload_dumper=${3:-payload-dumper-go}
image_dir="$output_dir/images"
tree_dir="$output_dir/partitions"

[[ -f "$ota_zip" ]] || { echo "OTA does not exist: $ota_zip" >&2; exit 1; }
command -v "$payload_dumper" >/dev/null 2>&1 || [[ -x "$payload_dumper" ]] || {
    echo "payload dumper is unavailable: $payload_dumper" >&2
    exit 1
}

mkdir -p "$image_dir" "$tree_dir"

available=$(
    "$payload_dumper" -l -m "$ota_zip" |
        awk -F: '{ print $1 }' |
        grep -E '^(system|system_ext|product|vendor|odm|my_product|my_manifest|my_region|my_carrier)$' || true
)
[[ -n "$available" ]] || {
    echo "OTA contains none of the required framework/vendor partitions" >&2
    exit 1
}
selected=$(printf '%s\n' "$available" | paste -sd, -)
printf 'extracting partitions: %s\n' "$selected" >&2
"$payload_dumper" -q -o "$image_dir" -p "$selected" "$ota_zip"

extract_image() {
    local partition=$1
    local image="$image_dir/$partition.img"
    local destination="$tree_dir/$partition"
    [[ -f "$image" ]] || return 0
    mkdir -p "$destination"

    local description
    description=$(file -b "$image")
    if [[ "$description" == *EROFS* ]]; then
        command -v fsck.erofs >/dev/null 2>&1 || {
            echo "fsck.erofs is required for $partition: $description" >&2
            exit 1
        }
        fsck.erofs --extract="$destination" "$image" >/dev/null
    elif [[ "$description" == *"Android sparse image"* ]]; then
        command -v simg2img >/dev/null 2>&1 || {
            echo "simg2img is required for $partition: $description" >&2
            exit 1
        }
        simg2img "$image" "$image.raw"
        debugfs -R "rdump / $destination" "$image.raw" >/dev/null 2>&1
        rm -f "$image.raw"
    else
        debugfs -R "rdump / $destination" "$image" >/dev/null 2>&1
    fi
}

for partition in system system_ext product vendor odm my_product my_manifest my_region my_carrier; do
    extract_image "$partition"
done

echo "$tree_dir"
