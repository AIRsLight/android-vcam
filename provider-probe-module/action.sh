#!/system/bin/sh

MODDIR=${0%/*}
STATE_DIR=/data/adb/android_vcam_provider_probe
PID_FILE=$STATE_DIR/provider.pid
LOG_FILE=$STATE_DIR/action.log
TRACE_FILE=$STATE_DIR/provider.trace
BINARY=$MODDIR/payload/bin/android.hardware.camera.provider-service-vcam-v2
LIBDIR=$MODDIR/payload/lib64
CONFIGDIR=$MODDIR/payload/empty-config
INSTANCE=android.hardware.camera.provider.ICameraProvider/vcam/0

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"

is_owned_process() {
    [ -s "$PID_FILE" ] || return 1
    pid="$(cat "$PID_FILE" 2>/dev/null)"
    case "$pid" in
        ''|*[!0-9]*) return 1 ;;
    esac
    [ -d "/proc/$pid" ] || return 1
    exe="$(readlink "/proc/$pid/exe" 2>/dev/null)"
    [ "$exe" = "$BINARY" ] || return 1
    return 0
}

if is_owned_process; then
    pid="$(cat "$PID_FILE")"
    echo "Stopping manual provider probe pid=$pid"
    kill "$pid"
    attempt=0
    while [ -d "/proc/$pid" ] && [ "$attempt" -lt 50 ]; do
        sleep 0.1
        attempt=$((attempt + 1))
    done
    if [ -d "/proc/$pid" ]; then
        echo "Probe did not stop after SIGTERM; leaving PID file for inspection"
        exit 1
    fi
    rm -f "$PID_FILE"
    echo "Provider probe stopped"
    exit 0
fi

rm -f "$PID_FILE" "$TRACE_FILE"
{
    echo "start $(date '+%Y-%m-%dT%H:%M:%S%z')"
    echo "context=$(cat /proc/self/attr/current 2>/dev/null)"
    echo "mode=manual-zero-camera"
} >> "$LOG_FILE"

export LD_LIBRARY_PATH="$LIBDIR:/vendor/lib64:/system/lib64:/system_ext/lib64:/product/lib64"
if [ "$ANDROID_VCAM_ENABLE_TRACE" = "1" ]; then
    export LD_PRELOAD="$LIBDIR/libprovider_probe_trace.so"
    export ANDROID_VCAM_PROBE_TRACE_PATH="$TRACE_FILE"
else
    unset LD_PRELOAD
    unset ANDROID_VCAM_PROBE_TRACE_PATH
fi
export ANDROID_VCAM_CONFIG_DIR="$CONFIGDIR/"
export ANDROID_VCAM_PROBE_SYSTEM_STABILITY=1

"$BINARY" </dev/null >>"$LOG_FILE" 2>&1 &
pid=$!
echo "$pid" > "$PID_FILE"

attempt=0
while [ "$attempt" -lt 50 ]; do
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "Provider probe exited during registration"
        cat "$TRACE_FILE" 2>/dev/null
        [ "$ANDROID_VCAM_ENABLE_TRACE" = "1" ] || \
            echo "Re-run with ANDROID_VCAM_ENABLE_TRACE=1 for Binder tracing"
        rm -f "$PID_FILE"
        exit 1
    fi
    if service check "$INSTANCE" 2>/dev/null | grep -q ': found$'; then
        echo "Provider probe registered as $INSTANCE (pid=$pid)"
        echo "It advertises zero cameras and will not restart after reboot"
        exit 0
    fi
    sleep 0.1
    attempt=$((attempt + 1))
done

echo "Provider process is alive but registration was not visible after 5 seconds"
cat "$TRACE_FILE" 2>/dev/null
exit 1
