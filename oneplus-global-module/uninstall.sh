#!/system/bin/sh

# Persistent provider and routing configuration intentionally survives removal.
# Removing the module restores both vendor paths from the read-only partition on
# the next boot because the OEM snapshot exists only inside the module tree.
rm -f /data/adb/android_vcam/mount.ok /data/adb/android_vcam/vcamd.pid
