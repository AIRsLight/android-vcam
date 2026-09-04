#!/system/bin/sh

# Build one PEM bundle from Android's system trust store for the statically
# linked Mbed TLS client. Callers provide a path inside their provider folder.
vcam_prepare_ca_bundle() {
    output="$1"
    temporary="$output.new-$$"
    rm -f "$temporary"
    umask 077

    found=0
    for directory in \
        /apex/com.android.conscrypt/cacerts \
        /system/etc/security/cacerts; do
        [ -d "$directory" ] || continue
        for certificate in "$directory"/*; do
            [ -f "$certificate" ] || continue
            cat "$certificate" >> "$temporary" || {
                rm -f "$temporary"
                return 1
            }
            printf '\n' >> "$temporary"
            found=1
        done
        [ "$found" = 1 ] && break
    done
    [ "$found" = 1 ] && [ -s "$temporary" ] || {
        rm -f "$temporary"
        echo "Android system CA store is unavailable" >&2
        return 1
    }

    mv -f "$temporary" "$output" || return 1
    chown camera:camera "$output" 2>/dev/null || true
    chmod 0640 "$output" || return 1
    chcon u:object_r:vcam_camera_data_file:s0 "$output" >/dev/null 2>&1 ||
        restorecon "$output" >/dev/null 2>&1 || true
}
