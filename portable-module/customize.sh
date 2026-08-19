#!/system/bin/sh

ui_print "- Validating portable cameraserver bootstrap"

sdk="$(getprop ro.build.version.sdk)"
abi="$(getprop ro.product.cpu.abi)"
[ "$sdk" = "34" ] || abort "! This prototype package is restricted to Android 14"
[ "$abi" = "arm64-v8a" ] || abort "! This prototype package requires arm64-v8a"

live_cameraserver="/system/bin/cameraserver"
launcher="$MODPATH/system/bin/cameraserver"
stock="$MODPATH/system/bin/vcam/cameraserver"
router="$MODPATH/system/lib64/libvcam_cameraserver_router.so"

for required in "$live_cameraserver" "$launcher" "$router"; do
    [ -f "$required" ] || abort "! Required file is missing: $required"
done

mkdir -p "${stock%/*}"
launcher_hash="$(sha256sum "$launcher" | awk '{print $1}')"
live_hash="$(sha256sum "$live_cameraserver" | awk '{print $1}')"
stock_source="$live_cameraserver"

if [ "$live_hash" = "$launcher_hash" ]; then
    stock_source=""
    for candidate in \
        "/data/adb/metamodule/mnt/$MODID/system/bin/vcam/cameraserver" \
        "/data/adb/modules/$MODID/system/bin/vcam/cameraserver"; do
        if [ -f "$candidate" ] && \
           [ "$(sha256sum "$candidate" | awk '{print $1}')" != "$launcher_hash" ]; then
            stock_source="$candidate"
            break
        fi
    done
    [ -n "$stock_source" ] || abort "! Existing stock cameraserver cannot be recovered"
fi

cp -fp "$stock_source" "$stock" || abort "! Unable to capture stock cameraserver"
stock_hash="$(sha256sum "$stock" | awk '{print $1}')"
[ "$stock_hash" != "$launcher_hash" ] || abort "! Captured stock aliases the launcher"

printf '%s  %s\n' "$stock_hash" "/system/bin/vcam/cameraserver" > "$MODPATH/stock.sha256"
printf '%s  %s\n' "$launcher_hash" "/system/bin/cameraserver" > "$MODPATH/launcher.sha256"

set_perm "$launcher" 0 2000 0755
set_perm "$stock" 0 2000 0755
set_perm "$router" 0 0 0644
chcon u:object_r:cameraserver_exec:s0 "$launcher" "$stock" || \
    abort "! Unable to label cameraserver executables"
chcon u:object_r:system_file:s0 "$router" || abort "! Unable to label router library"

ui_print "- Captured stock cameraserver: $stock_hash"
ui_print "- Bootstrap defaults to stock mode; no Binder routing is enabled"
