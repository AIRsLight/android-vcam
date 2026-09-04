#!/system/bin/sh

# Generate the runtime camera routing maps from one stable CameraService dump.
# The output is replaced atomically only after the complete topology validates.

set -u

DUMP_FILE=${1:-}
OUTPUT_DIR=${2:-/data/vendor/camera/vcam}
VCAM_DATA_CONTEXT=u:object_r:vcam_camera_data_file:s0

label_vcam_path() {
    # Host fixture tests do not run with Android SELinux mounted.
    [ -e /sys/fs/selinux/enforce ] || return 0
    chcon "$VCAM_DATA_CONTEXT" "$1"
}

if [ -z "$DUMP_FILE" ] || [ ! -r "$DUMP_FILE" ]; then
    echo "camera-map: readable CameraService dump required" >&2
    exit 2
fi

mkdir -p "$OUTPUT_DIR" || exit 3
label_vcam_path "$OUTPUT_DIR" || exit 3
TEMP_DIR="$OUTPUT_DIR/.camera-map.$$"
umask 077
mkdir "$TEMP_DIR" || exit 3
label_vcam_path "$TEMP_DIR" || exit 3
trap 'rm -rf "$TEMP_DIR"' EXIT HUP INT TERM

if ! awk \
        -v camera1_map="$TEMP_DIR/camera1-map.tsv" \
        -v camera1_targets="$TEMP_DIR/camera1-targets.tsv" \
        -v targets="$TEMP_DIR/targets.tsv" \
        -v topology="$TEMP_DIR/topology.conf" '
function invalid_id(value) {
    return value == "" || length(value) > 255 || value ~ /[[:space:]]/
}
function fail(message) {
    print "camera-map: " message > "/dev/stderr"
    invalid = 1
}
/Number of public camera devices visible to API1:/ {
    public_count = $NF
    if (public_count !~ /^[0-9]+$/) fail("invalid public Camera1 count")
    next
}
/^[[:space:]]*Device [0-9]+ maps to "[^"]+"[[:space:]]*$/ {
    camera_index = $2
    id = $5
    gsub(/^"|"$/, "", id)
    if (camera_index !~ /^[0-9]+$/ || invalid_id(id)) {
        fail("invalid Camera1 mapping")
        next
    }
    if (index_seen[camera_index]++) fail("duplicate Camera1 index " camera_index)
    if (id_seen[id]++) fail("duplicate Camera2 ID " id)
    mapping_count++
    mapping_id[mapping_count] = id
    mapping_index[mapping_count] = camera_index
    mapping_index_by_id[id] = camera_index
    next
}
/^== Camera HAL device [^ ]+ \(v[^)]*\) static information: ==$/ {
    current_id = $5
    sub(/^.*\//, "", current_id)
    if (invalid_id(current_id)) current_id = ""
    next
}
/^[[:space:]]*Facing:[[:space:]]*(Back|Front|External)[[:space:]]*$/ {
    if (current_id != "") {
        if (facing[current_id] != "" && facing[current_id] != $2) {
            fail("conflicting facing for " current_id)
        }
        facing[current_id] = $2
    }
    next
}
END {
    if (public_count == "") fail("public Camera1 count missing")
    if (mapping_count == 0) fail("Camera1 mapping table missing")
    if ((public_count + 0) > mapping_count) {
        fail("public Camera1 count exceeds mapping table")
    }
    if (!id_seen["1000"] || !id_seen["1001"]) {
        fail("internal cameras 1000/1001 are not both registered")
    }
    if (facing["1000"] != "Back" || facing["1001"] != "Front") {
        fail("internal camera facing does not match logical slots")
    }
    if (invalid) exit 10

    for (i = 1; i <= mapping_count; i++) {
        print mapping_id[i] "\t" mapping_index[i] > camera1_map
    }

    limit = public_count + 0
    if (limit > mapping_count) limit = mapping_count
    for (i = 1; i <= limit; i++) {
        id = mapping_id[i]
        if (id == "1000" || id == "1001") continue
        if (back_id == "" && facing[id] == "Back") {
            back_id = id
            back_index = mapping_index[i]
        } else if (front_id == "" && facing[id] == "Front") {
            front_id = id
            front_index = mapping_index[i]
        }
    }
    if (back_id == "") {
        fail("no public back camera found")
        exit 11
    }

    print back_id "\t0" > targets
    print back_index "\t0" > camera1_targets
    if (front_id != "") {
        print front_id "\t1" >> targets
        print front_index "\t1" >> camera1_targets
    }
    print "schema=1" > topology
    print "back_camera2_id=" back_id >> topology
    print "back_camera1_index=" back_index >> topology
    print "front_camera2_id=" (front_id == "" ? "none" : front_id) >> topology
    print "front_camera1_index=" (front_id == "" ? "none" : front_index) >> topology
    print "internal_back_camera1_index=" mapping_index_by_id["1000"] >> topology
    print "internal_front_camera1_index=" mapping_index_by_id["1001"] >> topology
}
' "$DUMP_FILE"; then
    echo "camera-map: topology validation failed" >&2
    exit 4
fi

# Populate the two internal-index fields without relying on awk array ordering.
internal_back_index=$(awk -F '\t' '$1 == "1000" {print $2}' \
    "$TEMP_DIR/camera1-map.tsv")
internal_front_index=$(awk -F '\t' '$1 == "1001" {print $2}' \
    "$TEMP_DIR/camera1-map.tsv")
[ -n "$internal_back_index" ] && [ -n "$internal_front_index" ] || exit 4
sed -i "s/^internal_back_camera1_index=.*/internal_back_camera1_index=$internal_back_index/" \
    "$TEMP_DIR/topology.conf"
sed -i "s/^internal_front_camera1_index=.*/internal_front_camera1_index=$internal_front_index/" \
    "$TEMP_DIR/topology.conf"

rm -f "$OUTPUT_DIR/topology.conf"
for name in targets.tsv camera1-targets.tsv camera1-map.tsv topology.conf; do
    mv -f "$TEMP_DIR/$name" "$OUTPUT_DIR/$name" || exit 3
    chown camera:camera "$OUTPUT_DIR/$name" 2>/dev/null || true
    chmod 0640 "$OUTPUT_DIR/$name" || exit 3
    label_vcam_path "$OUTPUT_DIR/$name" || exit 3
done

printf 'camera-map: ready back=%s front=%s\n' \
    "$(sed -n 's/^back_camera2_id=//p' "$OUTPUT_DIR/topology.conf")" \
    "$(sed -n 's/^front_camera2_id=//p' "$OUTPUT_DIR/topology.conf")"
