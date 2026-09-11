#!/usr/bin/env bash
# One-shot demo: start a local receiver, install the APK, grant permissions,
# start the 24/7 foreground telemetry service.
# Usage:
#   scripts/build_apk.sh
#   scripts/run_demo.sh            # endpoint http://10.0.2.2:8080/telemetry (emulator)
#   ENDPOINT=http://192.168.1.50:8080/telemetry scripts/run_demo.sh  # physical device
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APK="$ROOT/build/apk/app-debug.apk"
PKG="com.telemetry"
ENDPOINT="${ENDPOINT:-http://10.0.2.2:8080/telemetry}"

[ -f "$APK" ] || { echo "build the APK first: scripts/build_apk.sh" >&2; exit 1; }

echo "[receiver]   python3 $ROOT/scripts/http_endpoint.py &"
python3 "$ROOT/scripts/http_endpoint.py" &
RECV_PID=$!
trap 'kill $RECV_PID 2>/dev/null || true' EXIT

echo "[install]    $APK"
adb install -r "$APK"

echo "[permissions]"
adb shell pm grant "$PKG" android.permission.ACCESS_FINE_LOCATION || true
adb shell pm grant "$PKG" android.permission.ACCESS_COARSE_LOCATION || true
adb shell pm grant "$PKG" android.permission.POST_NOTIFICATIONS || true
adb shell appops set "$PKG" android:get_usage_stats allow || true   # Usage Access
adb shell appops set "$PKG" android:fine_location allow || true     # "All the time"
adb shell appops set "$PKG" android:coarse_location allow || true

echo "[start]      am start-foreground-service -e endpoint $ENDPOINT"
adb shell am set-stopped "$PKG" false || true
adb shell am start-foreground-service -n "$PKG/.TelemetryService" -e endpoint "$ENDPOINT"

echo
echo "Watch live:   tail -f $ROOT/build/received.jsonl"
echo "Watch device: adb logcat -s telemetry"
echo "Stop:         adb shell am stop-foreground -n $PKG/.TelemetryService"
wait $RECV_PID