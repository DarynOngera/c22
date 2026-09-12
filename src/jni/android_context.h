#pragma once

#include <jni.h>

#include <string>

#include <nlohmann/json.hpp>

namespace telemetry::jni {

// Called from JNI attach before telemetry starts.
void set_android_context(JNIEnv* env, jobject android_context);

// Connectivity/Telephony/Wifi summary: active network, signal strength, wifi rssi.
nlohmann::json jni_network_active();

// Recent apps via UsageStatsManager (requires Usage Access permission).
nlohmann::json jni_recent_apps();

}  // namespace telemetry::jni