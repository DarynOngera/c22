# Telemetry Service — Android (C++/JNI)

Background telemetry collector for Android, built as a **24/7 foreground service**
with the telemetry engine entirely in C++ via JNI. No Gradle — built with
`cmake --preset android` + a standalone APK assembly script.

---

## What it collects

| Type | Source | Interval |
|------|--------|----------|
| **health** | `/proc/stat`, `/proc/meminfo`, `/sys/class/power_supply/*`, `/sys/class/thermal/*`, `statvfs` | 10 s |
| **network** | `/proc/net/dev` (byte counters, rate) + JNI `ConnectivityManager`/`TelephonyManager`/`WifiManager` | 10 s |
| **app** | `/proc` (RSS per PID) + JNI `UsageStatsManager.queryUsageStats` (foreground apps, last-used) | 30 s |
| **location** | Live `LocationManager.requestLocationUpdates` (GPS + network providers, 30 s cadence) | per-fix |
| **event** | `telemetry::emit("name", {...})` from your own C++ code | on demand |

All messages are JSON: `{"ts", "device_id", "type", "data": {...}}`, batched
and POSTed to a configurable HTTPS endpoint via libcurl + OpenSSL.

---

## Prerequisites

- Android SDK + NDK 27.x (checked into `~/Android/Sdk/ndk/27.x`)
- `cmake ≥ 3.22`, `ninja`, `javac`, `keytool`, `python3`
- An emulator or physical device (arm64-v8a) with USB debugging enabled

---

## Quick start

```bash
# 1. Build the native library (first build compiles OpenSSL + curl, ~2-5 min)
cmake --preset android
cmake --build build/android

# 2. Assemble the APK (no Gradle)
scripts/build_apk.sh

# 3. Install + grant permissions + start the service
scripts/run_demo.sh          # uses emulator address 10.0.2.2:8080
ENDPOINT=http://192.168.1.50:8080/telemetry scripts/run_demo.sh
```

`run_demo.sh` starts a local HTTP receiver, installs the APK, grants all
permissions via `adb`, and starts the foreground service.

### Manual start (no helper script)

```bash
adb install build/apk/app-debug.apk
adb shell pm grant com.telemetry android.permission.ACCESS_FINE_LOCATION
adb shell pm grant com.telemetry android.permission.ACCESS_COARSE_LOCATION
adb shell pm grant com.telemetry android.permission.POST_NOTIFICATIONS
adb shell appops set com.telemetry android:get_usage_stats allow
adb shell appops set com.telemetry android:fine_location allow
adb shell am start-foreground-service -n com.telemetry/.TelemetryService \
    -e endpoint http://10.0.2.2:8080/telemetry
```

### Stop the service

```bash
adb shell am stop-foreground -n com.telemetry/.TelemetryService
```

---

## Architecture

```
TelemetryService (Java, START_STICKY, partial wake lock)
 └─ JNI_OnLoad → attach Engine
         │
    Engine (C++)
     ├── Scheduler threads
     │    ├── health (procfs) 10 s
     │    ├── network (procfs + JNI) 10 s
     │    ├── apps (/proc + JNI UsageStats) 30 s
     │    └── (location pushed by LocationListener)
     ├── Buffer (thread-safe deque, drop-oldest at cap)
     └── Sink thread ─► libcurl+OpenSSL HTTPS POST (retry+backoff)
```

### Build targets

| Preset | Output | Purpose |
|--------|--------|---------|
| `host` | `build/host/telemetry_tests` | Catch2 unit tests (run on Linux) |
| `android` | `build/android/libtelemetry.so` | ARM64 shared lib for Android |

### Test (host)

```bash
cmake --preset host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

---

## Permissions

| Permission | Why | How to grant |
|------------|-----|--------------|
| `ACCESS_FINE_LOCATION` | Live GPS fix | `pm grant` + `appops set ... allow` for "All the time" |
| `PACKAGE_USAGE_STATS` | Recent-foreground-app list | `appops set ... allow` (Usage Access) |
| `POST_NOTIFICATIONS` | Persistent foreground-service notification (Android 13+) | `pm grant` |
| `INTERNET` | Send telemetry to endpoint | auto-granted at install |
| `RECEIVE_BOOT_COMPLETED` | Restart service after reboot | auto-granted at install |

Without Usage Access the service still runs; the app list will simply be empty.
Without fine location "all the time" the `LocationListener` receives no fixes
when the activity is backgrounded (foreground only).

---

## Configuration

Edit `src/core/engine.h` defaults or override at runtime:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `endpoint` | `http://10.0.2.2:8080/telemetry` | POST target (override via `am start-foreground-service -e endpoint URL`) |
| `device_id` | `Settings.Secure.ANDROID_ID` | Unique per app signing key + user |
| `buffer_max` | 4096 | Max messages before drop-oldest kicks in |
| `batch_max` | 200 | Messages per HTTP POST |
| `flush_interval_ms` | 15 000 | Idle cadence for the sink thread |
| `tls_verify` | true | Verify server certificate (curl `CURLOPT_SSL_VERIFYPEER`) |

---

## Project layout

```
c22/
  CMakeLists.txt                  # presets: host (tests) + android (libtelemetry.so)
  CMakePresets.json               # android NDK 27.x, arm64-v8a
  cmake/deps.cmake                # OpenSSL + libcurl (external projects)
  AndroidManifest.xml
  java/com/telemetry/
    TelemetryService.java         # 24/7 foreground service (thin Java shell)
    Telemetry.java                # native method declarations
    TelemetryLocationListener.java
    BootReceiver.java
  include/telemetry/              # public API (engine.h, config.h, event.h)
  src/
    core/                         # engine, buffer, scheduler, procfs, types
    jni/                          # JNI util, android context, native bridge
    transport/                    # http_client interface + curl impl
  tests/                          # Catch2 unit tests (host-only)
  third_party/                    # nlohmann/json, catch2, curl/openssl tarballs
  scripts/
    build_apk.sh                  # no-Gradle APK assembly
    run_demo.sh                   # end-to-end demo launcher
    http_endpoint.py              # local telemetry receiver (python)
```

---

## Limitations

* **Foreground service only** — `NativeActivity` apps cannot persist as 24/7
  background services; a thin Java `Service` host is required and included.
* **Android 12+** — `BOOT_COMPLETED` cannot start a service from a "stopped" app
  until the user (or `adb`) opens it once. Use `adb shell am set-stopped ... false`.
* **Doze mode** — network traffic may be delayed by Doze. Partial wake lock
  mitigates this while the screen is on; full bypass requires an `AlarmManager`
  wake-up or `DeviceAdmin` integration (not in scope here).
* **Package visibility** — Android 11+ restricts `queryUsageStats`. The service
  has `PACKAGE_USAGE_STATS` permission; no further visibility declarations are
  needed because it queries usage stats directly.
