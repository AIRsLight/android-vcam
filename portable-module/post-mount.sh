#!/system/bin/sh

MODDIR=${0%/*}
LOG_DIR=/data/adb/android_vcam_portable
LOG_FILE=$LOG_DIR/bootstrap.log
LIVE=/system/bin/cameraserver
STOCK=/system/bin/vcam/cameraserver
ROUTER=/system/lib64/libvcam_cameraserver_router.so
MODE=/system/etc/android_vcam/bootstrap.mode
RUNTIME_DIR=/dev/vcam

mkdir -p "$LOG_DIR"
chmod 0700 "$LOG_DIR"

fail_bootstrap() {
    echo "post-mount: $1" >> "$LOG_FILE"
    touch "$MODDIR/disable"
    if [ -x "$STOCK" ] && [ -e "$LIVE" ]; then
        mount -o bind "$STOCK" "$LIVE" 2>> "$LOG_FILE" && \
            echo "post-mount: stock cameraserver rebound for this boot" >> "$LOG_FILE"
    fi
    exit 1
}

[ ! -e "$MODDIR/disable" ] || exit 0
[ -r "$MODDIR/launcher.sha256" ] || fail_bootstrap "launcher manifest missing"
[ -r "$MODDIR/stock.sha256" ] || fail_bootstrap "stock manifest missing"

expected_launcher="$(awk 'NR == 1 {print $1}' "$MODDIR/launcher.sha256")"
expected_stock="$(awk 'NR == 1 {print $1}' "$MODDIR/stock.sha256")"
actual_launcher="$(sha256sum "$LIVE" 2>/dev/null | awk '{print $1}')"
actual_stock="$(sha256sum "$STOCK" 2>/dev/null | awk '{print $1}')"

[ "$actual_launcher" = "$expected_launcher" ] || \
    fail_bootstrap "launcher hash mismatch: $actual_launcher"
[ "$actual_stock" = "$expected_stock" ] || \
    fail_bootstrap "stock hash mismatch: $actual_stock"
[ -r "$ROUTER" ] || fail_bootstrap "router library missing"
configured_mode="$(sed -n '1p' "$MODE" 2>/dev/null)"
case "$configured_mode" in
    stock|preflight|passthrough|physical-route) ;;
    *) fail_bootstrap "invalid bootstrap mode: $configured_mode" ;;
esac

live_context="$(ls -Zd "$LIVE" 2>/dev/null | awk '{print $1}')"
stock_context="$(ls -Zd "$STOCK" 2>/dev/null | awk '{print $1}')"
[ "$live_context" = "u:object_r:cameraserver_exec:s0" ] || \
    fail_bootstrap "launcher context mismatch: $live_context"
[ "$stock_context" = "u:object_r:cameraserver_exec:s0" ] || \
    fail_bootstrap "stock context mismatch: $stock_context"

mkdir -p "$RUNTIME_DIR" || fail_bootstrap "runtime directory creation failed"
chown cameraserver:camera "$RUNTIME_DIR"
chmod 0770 "$RUNTIME_DIR"
chcon u:object_r:cameraserver_tmpfs:s0 "$RUNTIME_DIR" || \
    fail_bootstrap "runtime directory labeling failed"

{
    echo "post-mount $(date '+%Y-%m-%dT%H:%M:%S%z')"
    echo "launcher=$actual_launcher"
    echo "stock=$actual_stock"
    echo "mode=stock-default"
} >> "$LOG_FILE"
