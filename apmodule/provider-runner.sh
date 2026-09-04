#!/system/bin/sh

MODDIR=${0%/*}
STATE_DIR="/data/adb/android_vcam/providers"
FRAME_DIR="/data/vendor/camera/vcam/providers"
id="$1"

case "$id" in ''|*[!A-Za-z0-9._-]*) exit 64 ;; esac
meta="$STATE_DIR/$id/meta"
[ -f "$meta" ] || exit 66

type=$(sed -n 's/^type=//p' "$meta" | head -n 1)
source_b64=$(sed -n 's/^source_b64=//p' "$meta" | head -n 1)
source=$(printf '%s' "$source_b64" | base64 -d 2>/dev/null) || exit 65
frame="$FRAME_DIR/$id/frame.rgb"
enabled="$FRAME_DIR/$id/enabled"
fps=$(sed -n 's/^fps=//p' "$meta" | head -n 1)
max_width=$(sed -n 's/^max_width=//p' "$meta" | head -n 1)
max_height=$(sed -n 's/^max_height=//p' "$meta" | head -n 1)
: "${fps:=15}"
: "${max_width:=1280}"
: "${max_height:=720}"

cleanup() {
    rm -f "$enabled"
}
trap cleanup EXIT HUP INT TERM

find_streamer() {
    for candidate in \
        "$MODDIR/system/bin/vcam-streamer" \
        "$MODDIR/system/system/bin/vcam-streamer" \
        "$MODDIR/vendor/bin/vcam-streamer" \
        "$MODDIR/system/vendor/bin/vcam-streamer"; do
        [ -x "$candidate" ] && { echo "$candidate"; return 0; }
    done
    return 1
}

streamer=$(find_streamer) || exit 69
run_privileged_tool() {
    context=$(cat /proc/self/attr/current 2>/dev/null)
    case "$context" in
        u:r:vcamd:s0*)
            /system/bin/runcon u:r:magisk:s0 "$@"
            ;;
        *)
            "$@"
            ;;
    esac
}
run_streamer() {
    run_privileged_tool "$streamer" \
        "$1" "$frame" "$fps" "$max_width" "$max_height"
}
case "$type" in
    https)
        cache="$FRAME_DIR/$id/remote-video.cache"
        temporary="$cache.new"
        rm -f "$temporary"
        if ! run_privileged_tool /system/bin/curl \
            --location --fail --silent --show-error \
            --connect-timeout 15 --retry 3 --output "$temporary" "$source"; then
            rm -f "$temporary"
            exit 69
        fi
        mv -f "$temporary" "$cache"
        chown camera:camera "$cache"
        chmod 0640 "$cache"
        if selinux_context=$(ls -Zd "$FRAME_DIR" 2>/dev/null | awk '{print $1}'); then
            chcon "$selinux_context" "$cache" >/dev/null 2>&1 || true
        fi
        run_streamer "$cache"
        ;;
    http|hls|rtsp|video)
        run_streamer "$source"
        ;;
    *)
        exit 64
        ;;
esac
