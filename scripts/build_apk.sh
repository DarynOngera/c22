#!/usr/bin/env bash
# Assemble a no-Gradle APK: javac -> d8 -> aapt2 -> zip -> zipalign -> apksigner.
# Usage: scripts/build_apk.sh [path/to/libtelemetry.so]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK="${ANDROID_HOME:-$HOME/Android/Sdk}"
[ -d "$SDK" ] || { echo "Android SDK not found at $SDK" >&2; exit 1; }

BT="$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)"
PLATFORM="$SDK/platforms/android-35/android.jar"
[ -f "$PLATFORM" ] || { echo "android-35 platform jar missing" >&2; exit 1; }

OUT="$ROOT/build/apk"
SO="${1:-}"
if [ -z "$SO" ]; then
  SO="$(find "$ROOT/build/android" -name libtelemetry.so -print -quit 2>/dev/null || true)"
fi
[ -n "$SO" ] || { echo "libtelemetry.so not found (run: cmake --preset android && cmake --build build/android)" >&2; exit 1; }

mkdir -p "$OUT/classes" "$OUT/dex"
rm -f "$OUT"/*.apk "$OUT"/classes/com -r

echo "[1/6] javac (against android-35)"
javac --release 17 -classpath "$PLATFORM" -d "$OUT/classes" "$ROOT"/java/com/telemetry/*.java

echo "[2/6] d8 -> classes.dex"
"$BT/d8" --release --lib "$PLATFORM" --output "$OUT/dex" "$OUT"/classes/com/telemetry/*.class

echo "[3/6] aapt2 link manifest -> base.apk"
"$BT/aapt2" link -o "$OUT/base.apk" -I "$PLATFORM" \
  --manifest "$ROOT/AndroidManifest.xml" \
  --min-sdk-version 29 --target-sdk-version 35 --auto-add-overlay

echo "[4/6] add classes.dex + libtelemetry.so"
python3 - "$OUT/base.apk" "$OUT/dex/classes.dex" "$SO" <<'PY'
import sys, os, zipfile
apk, dex, so = sys.argv[1], sys.argv[2], sys.argv[3]
tmp = apk + ".tmp"
with zipfile.ZipFile(apk, "r") as zin, zipfile.ZipFile(tmp, "w") as zout:
    for item in zin.infolist():
        zout.writestr(item, zin.read(item.filename))
    zout.write(dex, "classes.dex", compress_type=zipfile.ZIP_DEFLATED)
    zout.write(so, "lib/arm64-v8a/libtelemetry.so", compress_type=zipfile.ZIP_STORED)
os.replace(tmp, apk)
PY

echo "[5/6] zipalign"
"$BT/zipalign" -f 4 "$OUT/base.apk" "$OUT/aligned.apk"

echo "[6/6] sign (debug keystore)"
KS="$ROOT/build/debug.keystore"
if [ ! -f "$KS" ]; then
  keytool -genkeypair -keystore "$KS" -alias androiddebugkey \
    -storepass android -keypass android -keyalg RSA -keysize 2048 \
    -dname "CN=Android Debug,O=Android,C=US" -validity 10000
fi
"$BT/apksigner" sign --ks "$KS" --ks-key-alias androiddebugkey \
  --ks-pass pass:android --key-pass pass:android \
  --out "$OUT/app-debug.apk" "$OUT/aligned.apk"

echo "APK ready: $OUT/app-debug.apk"
"$BT/apksigner" verify --print-certs "$OUT/app-debug.apk" >/dev/null 2>&1 && echo "signature OK"