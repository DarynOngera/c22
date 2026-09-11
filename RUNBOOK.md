# Telemetry Service — Technical Runbook

All commands to build, deploy, operate, debug and tear down the telemetry service on a connected Android device.

---

## Prerequisites

```bash
# Verify SDK / NDK
ls $HOME/Android/Sdk/ndk/27.1.12297006
ls $HOME/Android/Sdk/platforms/android-35/android.jar
ls $HOME/Android/Sdk/build-tools/36.0.0/aapt2

# Verify adb sees the device
adb devices -l
# Should show: <serial>  device usb:<bus>  product:<codename>  model:<model>  device:<device>
```

---

## Build

### Host tests (Linux x86_64)

```bash
cmake --preset host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
# Binary: build/host/telemetry_tests
```

### Android native library (arm64-v8a)

```bash
# First build: compiles OpenSSL + curl (~2-5 min)
cmake --preset android
cmake --build build/android

# Output: build/android/libtelemetry.so (15 MB ARM64)
```

### APK assembly (no Gradle)

```bash
# Requires: javac, d8, aapt2, zipalign, apksigner (all from Android SDK)
scripts/build_apk.sh build/android/libtelemetry.so

# Output: build/apk/app-debug.apk
# The script auto-discovers: AndroidManifest.xml, java/**/*.java, classes.dex,
# libtelemetry.so under build/android/, debug.keystore for signing.
```

### Full rebuild (clean)

```bash
rm -rf build/host build/android build/apk build/received.jsonl
cmake --preset android && cmake --build build/android
scripts/build_apk.sh build/android/libtelemetry.so
```

---

## Deploy

### Install

```bash
# Standard install (requires "Install via USB" on Xiaomi/HyperOS)
adb install -r build/apk/app-debug.apk

# If Xiaomi Security Center dialog pops up, tap "Install / Continue" on the phone
```

### Uninstall

```bash
# Stop service first
adb shell am force-stop com.telemetry
# Uninstall
adb uninstall com.telemetry
```

---

## Permissions

### Grant via adb (requires "USB debugging (Security settings)" on Xiaomi)

```bash
adb shell pm grant com.telemetry android.permission.ACCESS_FINE_LOCATION
adb shell pm grant com.telemetry android.permission.ACCESS_COARSE_LOCATION
adb shell pm grant com.telemetry android.permission.POST_NOTIFICATIONS
```

### Grant via appops (no special permission needed)

```bash
adb shell appops set com.telemetry android:get_usage_stats allow
adb shell appops set com.telemetry android:fine_location allow
adb shell appops set com.telemetry android:coarse_location allow
```

### Grant Usage Access via Settings UI

```bash
# Opens the app info page; tap "Permissions" -> "Location" -> "Allow all the time"
adb shell am start -a android.settings.APPLICATION_DETAILS_SETTINGS -d package:com.telemetry
```

### Check granted permissions

```bash
adb shell dumpsys package com.telemetry | grep -E "granted=true|granted=false"
adb shell appops get com.telemetry | grep -E "FINE_LOCATION|COARSE_LOCATION|GET_USAGE_STATS"
```

---

## Start / Stop Service

### Start foreground service

```bash
# Default endpoint (emulator loopback):
adb shell am start-foreground-service -n com.telemetry/.TelemetryService

# Custom endpoint:
adb shell am start-foreground-service \
  -n com.telemetry/.TelemetryService \
  -e endpoint http://<HOST_IP>:8080/telemetry
```

### Stop service

```bash
adb shell am stop-foreground -n com.telemetry/.TelemetryService
# or
adb shell am stopservice -n com.telemetry/.TelemetryService
# or force-stop (kills process entirely):
adb shell am force-stop com.telemetry
```

### Network routing for real device

The emulator address `10.0.2.2` does not work on a physical device. Options:

```bash
# Option A: adb reverse (device 127.0.0.1 -> host 127.0.0.1)
adb reverse tcp:8080 tcp:8080
# Then use: -e endpoint http://127.0.0.1:8080/telemetry

# Option B: WiFi LAN (both on same subnet)
adb shell ip addr show wlan0 | grep inet   # get device IP
ip -4 addr show | grep inet | grep -v 127  # get host IP
# Then use: -e endpoint http://<HOST_IP>:8080/telemetry

# Check adb reverse list
adb reverse --list

# Remove a reverse
adb reverse --remove tcp:8080
```

---

## Run the demo (all-in-one)

```bash
# Start receiver, install, grant permissions, start service
scripts/run_demo.sh

# With custom endpoint:
ENDPOINT=http://192.168.82.124:8080/telemetry scripts/run_demo.sh
```

### Manual receiver

```bash
python3 scripts/http_endpoint.py
# Listens on 0.0.0.0:8080, writes to build/received.jsonl

# Custom port:
TELEMETRY_PORT=9090 python3 scripts/http_endpoint.py
```

---

## View Logs

### Telemetry native logs (logcat tag: "telemetry")

```bash
# Filtered (recommended)
adb logcat -s telemetry

# With timestamps
adb logcat -v threadtime -s telemetry

# Last 50 lines
adb logcat -d -s telemetry | tail -50

# Search for specific events
adb logcat -d -s telemetry | grep -E "posted|failed|started|stopped"
```

### Full crash log

```bash
# Live crash capture (run in background, then trigger service start)
adb logcat -c
adb logcat -s DEBUG:V libc:V AndroidRuntime:E telemetry:V
```

### Service state

```bash
# Check if service is running
adb shell dumpsys activity services com.telemetry

# Check if process is alive
adb shell pidof com.telemetry

# Check process details
adb shell ps -A | grep telemetry
adb shell cat /proc/$(adb shell pidof com.telemetry)/status | grep -E "Name|State|Threads"

# Foreground notification
adb shell dumpsys activity services com.telemetry | grep -E "isForeground|foregroundId"
```

### Connectivity / telemetry data

```bash
# Monitor received.jsonl in real time
tail -f build/received.jsonl

# Pretty-print received JSON
python3 -c "
import json, sys
for line in open('build/received.jsonl'):
    print(json.dumps(json.loads(line), indent=2))
"

# Count events by type
python3 -c "
import json, collections
c = collections.Counter()
for line in open('build/received.jsonl'):
    c[json.loads(line).get('type','')] += 1
for t, n in c.most_common(): print(f'{t:12} {n}')
"
```

---

## Debugging

### Common issues

| Symptom | Cause | Fix |
|---------|-------|-----|
| `INSTALL_FAILED_USER_RESTRICTED` | Xiaomi "Install via USB" off or dialog not confirmed | Enable in Developer Options; tap "Continue" on Security Center dialog |
| `Requires permission not exported` | Service not exported or app not debuggable | Set `android:exported="true"` or `android:debuggable="true"` in manifest |
| `SecurityException: Starting FGS with type location` | Location runtime permission not granted | Grant via Settings UI or degrade to dataSync-only FGS type |
| `post failed: Connection timed out` | Device cannot reach host IP | Use `adb reverse` or verify WiFi LAN connectivity |
| `engine started: endpoint=http://10.0.2.2` | Intent extra not reaching service | Ensure `-e endpoint URL` is in the `am start-foreground-service` command |
| Empty batches (`received 0 event(s): []`) | `sched_->start()` never called or samplers not registered | Ensure engine.cpp calls `sched_->start()` after `start_samplers()` |

### Check TCP connections from device

```bash
adb shell cat /proc/net/tcp    # hex-encoded local/remote addresses
# Decode: 4552A8C0:E718 = 192.168.82.69:59160 (hex bytes reversed)
```

### Device info

```bash
adb shell getprop ro.product.cpu.abi          # arm64-v8a
adb shell getprop ro.build.version.sdk        # 36
adb shell getprop ro.build.version.release    # 16
adb shell getprop ro.build.fingerprint        # full build string
adb shell getprop ro.product.model            # device model
```

### Verify APK contents

```bash
python3 -c "
import zipfile
with zipfile.ZipFile('build/apk/app-debug.apk') as z:
    for i in z.infolist():
        print(f'{i.compress_type:5} {i.file_size:>12} {i.filename}')
"
# Expected: AndroidManifest.xml, classes.dex, lib/arm64-v8a/libtelemetry.so, META-INF/*
```

### Verify installed package

```bash
adb shell dumpsys package com.telemetry | grep -E "versionName|flags|debuggable|targetSdk|minSdk"
```

### Verify native library on device

```bash
# Pull and inspect
adb pull /data/app/*/com.telemetry*/lib/arm64/libtelemetry.so /tmp/libtelemetry.so
file /tmp/libtelemetry.so
nm -D /tmp/libtelemetry.so | grep -E "Java_com_telemetry|JNI_OnLoad"
```

---

## File layout reference

```
build/android/libtelemetry.so      # Cross-compiled ARM64 shared library
build/android/third_party/install/  # OpenSSL + curl static archives
build/apk/app-debug.apk            # Signed APK ready to install
build/host/telemetry_tests         # Host unit test binary
build/received.jsonl                # Telemetry data captured by http_endpoint.py
```
