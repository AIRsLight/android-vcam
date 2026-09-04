#!/system/bin/sh

MODDIR=${0%/*}

echo "Refreshing read-only compatibility report"
sh "$MODDIR/run-probe.sh"
