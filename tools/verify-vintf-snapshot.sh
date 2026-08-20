#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: verify-vintf-snapshot.sh --checkvintf PATH --snapshot PATH [options]

Run an Android host checkvintf binary against a pulled partition snapshot.
The snapshot must contain system/, vendor/, product/, system_ext/, odm/ and
apex/apex-info-list.xml.

Options:
  --first-api N          ro.product.first_api_level (required)
  --vendor-sku VALUE     ro.boot.product.vendor.sku (default: empty)
  --hardware-sku VALUE   ro.boot.product.hardware.sku (default: empty)
  -h, --help             show this help
EOF
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

checkvintf=
snapshot=
first_api=
vendor_sku=
hardware_sku=

while (($#)); do
    case "$1" in
        --checkvintf)
            (($# >= 2)) || fail "--checkvintf requires a value"
            checkvintf=$2
            shift 2
            ;;
        --snapshot)
            (($# >= 2)) || fail "--snapshot requires a value"
            snapshot=$2
            shift 2
            ;;
        --first-api)
            (($# >= 2)) || fail "--first-api requires a value"
            first_api=$2
            shift 2
            ;;
        --vendor-sku)
            (($# >= 2)) || fail "--vendor-sku requires a value"
            vendor_sku=$2
            shift 2
            ;;
        --hardware-sku)
            (($# >= 2)) || fail "--hardware-sku requires a value"
            hardware_sku=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown argument: $1"
            ;;
    esac
done

[[ -x "$checkvintf" ]] || fail "checkvintf is not executable: $checkvintf"
[[ -d "$snapshot" ]] || fail "snapshot directory is missing: $snapshot"
[[ "$first_api" =~ ^[0-9]+$ ]] || fail "--first-api must be a non-negative integer"
snapshot=$(cd -- "$snapshot" && pwd)

for partition in system vendor product system_ext odm; do
    [[ -d "$snapshot/$partition" ]] || fail "snapshot is missing $partition/"
done
[[ -f "$snapshot/apex/apex-info-list.xml" ]] || \
    fail "snapshot is missing apex/apex-info-list.xml"

command=(
    "$checkvintf"
    --check-compat
    --dirmap "/system:$snapshot/system"
    --dirmap "/vendor:$snapshot/vendor"
    --dirmap "/product:$snapshot/product"
    --dirmap "/system_ext:$snapshot/system_ext"
    --dirmap "/odm:$snapshot/odm"
    --dirmap "/apex:$snapshot/apex"
    --property "ro.product.first_api_level=$first_api"
    --property "ro.boot.product.hardware.sku=$hardware_sku"
    --property "ro.boot.product.vendor.sku=$vendor_sku"
)

log_file=$(mktemp)
trap 'rm -f -- "$log_file"' EXIT HUP INT TERM
set +e
"${command[@]}" >"$log_file" 2>&1
status=$?
set -e

tail -n 80 "$log_file"
if ((status != 0)); then
    printf 'checkvintf failed with status %d\n' "$status" >&2
    exit "$status"
fi
grep -qx 'COMPATIBLE' "$log_file" || fail "checkvintf did not print COMPATIBLE"
