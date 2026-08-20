#!/system/bin/sh

MODDIR=$1
COMMAND=$2
STATE_DIR=/data/adb/android_vcam_hidl_provider
PID_FILE=$STATE_DIR/provider.pid
MODE_FILE=$STATE_DIR/provider.mode
LOG_FILE=$STATE_DIR/provider.log
BINARY=$MODDIR/payload/bin/vcam_hidl_provider
MODULE=$MODDIR/payload/lib64/hw/camera.vcam.so
LIBDIR=$MODDIR/payload/lib64
INSTANCE='android.hardware.camera.provider@2.4::ICameraProvider/vcam/0'

mkdir -p "$STATE_DIR"
chmod 0700 "$STATE_DIR"

is_owned_process() {
    [ -s "$PID_FILE" ] || return 1
    pid="$(cat "$PID_FILE" 2>/dev/null)"
    case "$pid" in
        ''|*[!0-9]*) return 1 ;;
    esac
    [ -d "/proc/$pid" ] || return 1
    [ "$(readlink "/proc/$pid/exe" 2>/dev/null)" = "$BINARY" ] || return 1
    return 0
}

stop_provider() {
    if ! is_owned_process; then
        rm -f "$PID_FILE" "$MODE_FILE"
        return 0
    fi
    pid="$(cat "$PID_FILE")"
    kill "$pid" 2>/dev/null
    attempt=0
    while [ -d "/proc/$pid" ] && [ "$attempt" -lt 50 ]; do
        sleep 0.1
        attempt=$((attempt + 1))
    done
    if [ -d "/proc/$pid" ]; then
        echo "provider did not stop after SIGTERM: pid=$pid" >&2
        return 1
    fi
    rm -f "$PID_FILE" "$MODE_FILE"
    return 0
}

start_provider() {
    requested_mode=$1
    stop_provider || return 1

    export LD_LIBRARY_PATH="$LIBDIR:/vendor/lib64:/system/lib64:/system_ext/lib64:/product/lib64"
    export ANDROID_VCAM_HIDL_MODULE_PATH="$MODULE"
    unset ANDROID_VCAM_HIDL_ALLOW_UNDECLARED
    if [ "$requested_mode" = "two" ]; then
        export ANDROID_VCAM_HIDL_ADVERTISE_CAMERAS=1
    else
        unset ANDROID_VCAM_HIDL_ADVERTISE_CAMERAS
    fi

    {
        echo "start $(date '+%Y-%m-%dT%H:%M:%S%z')"
        echo "mode=$requested_mode"
        echo "context=$(cat /proc/self/attr/current 2>/dev/null)"
    } >> "$LOG_FILE"

    "$BINARY" </dev/null >>"$LOG_FILE" 2>&1 &
    pid=$!
    echo "$pid" > "$PID_FILE"
    echo "$requested_mode" > "$MODE_FILE"

    attempt=0
    while [ "$attempt" -lt 100 ]; do
        if ! kill -0 "$pid" 2>/dev/null; then
            echo "provider exited during registration; see $LOG_FILE" >&2
            rm -f "$PID_FILE" "$MODE_FILE"
            return 1
        fi
        if lshal 2>/dev/null | grep -Fq "$INSTANCE"; then
            echo "provider registered: pid=$pid mode=$requested_mode"
            return 0
        fi
        sleep 0.1
        attempt=$((attempt + 1))
    done

    echo "provider registration timed out; see $LOG_FILE" >&2
    stop_provider
    return 1
}

case "$COMMAND" in
    start-zero) start_provider zero ;;
    start-two) start_provider two ;;
    stop) stop_provider ;;
    status)
        if is_owned_process; then
            echo "running pid=$(cat "$PID_FILE") mode=$(cat "$MODE_FILE" 2>/dev/null)"
        else
            echo "stopped"
            exit 1
        fi
        ;;
    *)
        echo "usage: provider-control.sh MODDIR {start-zero|start-two|stop|status}" >&2
        exit 2
        ;;
esac
