#include "jni/android_context.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include <android/log.h>

#include "core/types.h"
#include "jni/jni_util.h"

namespace telemetry::jni {
namespace {

jobject g_ctx_global = nullptr;
std::string g_device_id = "unknown";

jobject get_system_service(JNIEnv* env, const char* name) {
  if (!g_ctx_global) return nullptr;
  jclass ctx_cls = env->GetObjectClass(g_ctx_global);
  if (!ctx_cls) return nullptr;
  jmethodID mid = env->GetMethodID(
      ctx_cls, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
  env->DeleteLocalRef(ctx_cls);
  if (!mid) return nullptr;
  jstring name_j = env->NewStringUTF(name);
  jobject svc = env->CallObjectMethod(g_ctx_global, mid, name_j);
  env->DeleteLocalRef(name_j);
  return svc;
}

}  // namespace

void set_android_context(JNIEnv* env, jobject android_context,
                         const std::string& device_id) {
  if (g_ctx_global) env->DeleteGlobalRef(g_ctx_global);
  g_ctx_global = env->NewGlobalRef(android_context);
  if (!device_id.empty()) g_device_id = device_id;
}

std::string device_id() { return g_device_id; }

nlohmann::json jni_network_active() {
  nlohmann::json j;
  Env env;
  if (!env) return j;

  if (jobject cm = get_system_service(env.get(), "connectivity")) {
    jclass cls = env->GetObjectClass(cm);
    jmethodID active =
        env->GetMethodID(cls, "getActiveNetworkInfo", "()Landroid/net/NetworkInfo;");
    if (active) {
      jobject ni = env->CallObjectMethod(cm, active);
      if (ni && !env->ExceptionCheck()) {
        jclass ni_cls = env->GetObjectClass(ni);
        jmethodID type_name =
            env->GetMethodID(ni_cls, "getTypeName", "()Ljava/lang/String;");
        jmethodID subtype =
            env->GetMethodID(ni_cls, "getSubtypeName", "()Ljava/lang/String;");
        if (type_name) {
          jstring s = (jstring)env->CallObjectMethod(ni, type_name);
          if (s) {
            j["type"] = to_utf(env.get(), s);
            env->DeleteLocalRef(s);
          }
        }
        if (subtype) {
          jstring s = (jstring)env->CallObjectMethod(ni, subtype);
          if (s) {
            j["subtype"] = to_utf(env.get(), s);
            env->DeleteLocalRef(s);
          }
        }
        env->DeleteLocalRef(ni_cls);
      }
      if (ni) env->DeleteLocalRef(ni);
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(cm);
  }
  clear_exception(env.get(), "network_active[connectivity]");

  if (jobject tm = get_system_service(env.get(), "telephony")) {
    jclass cls = env->GetObjectClass(tm);
    jmethodID get_sig =
        env->GetMethodID(cls, "getSignalStrength", "()Landroid/telephony/SignalStrength;");
    if (get_sig) {
      jobject ss = env->CallObjectMethod(tm, get_sig);
      if (ss && !env->ExceptionCheck()) {
        jclass ss_cls = env->GetObjectClass(ss);
        jmethodID level = env->GetMethodID(ss_cls, "getLevel", "()I");
        if (level) {
          jint lv = env->CallIntMethod(ss, level);
          if (!env->ExceptionCheck()) j["mobile_signal_level"] = lv;  // 0..4
        }
        jmethodID dbm =
            env->GetMethodID(ss_cls, "getCellSignalStrength", "()Landroid/telephony/CellSignalStrength;");
        if (dbm) {
          jobject css = env->CallObjectMethod(ss, dbm);
          if (css && !env->ExceptionCheck()) {
            jclass css_cls = env->GetObjectClass(css);
            jmethodID get_dbm = env->GetMethodID(css_cls, "getDbm", "()I");
            if (get_dbm) {
              jint d = env->CallIntMethod(css, get_dbm);
              if (!env->ExceptionCheck()) j["mobile_signal_dbm"] = d;
            }
            env->DeleteLocalRef(css_cls);
          }
          if (css) env->DeleteLocalRef(css);
        }
        env->DeleteLocalRef(ss_cls);
      }
      if (ss) env->DeleteLocalRef(ss);
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(tm);
  }
  clear_exception(env.get(), "network_active[telephony]");

  if (jobject wm = get_system_service(env.get(), "wifi")) {
    jclass cls = env->GetObjectClass(wm);
    jmethodID conn =
        env->GetMethodID(cls, "getConnectionInfo", "()Landroid/net/wifi/WifiInfo;");
    if (conn) {
      jobject info = env->CallObjectMethod(wm, conn);
      if (info && !env->ExceptionCheck()) {
        jclass info_cls = env->GetObjectClass(info);
        jmethodID get_rssi = env->GetMethodID(info_cls, "getRssi", "()I");
        jmethodID get_wifi_link = env->GetMethodID(info_cls, "getLinkSpeed", "()I");
        if (get_rssi) {
          jint rssi = env->CallIntMethod(info, get_rssi);
          if (!env->ExceptionCheck() && rssi > -200 && rssi < 0) {
            j["wifi_rssi_dbm"] = rssi;
            j["wifi_signal_pct"] = std::max(0, std::min(100, (rssi + 127) * 100 / 127));
          }
        }
        if (get_wifi_link) {
          jint link = env->CallIntMethod(info, get_wifi_link);
          if (!env->ExceptionCheck() && link > 0) j["wifi_link_mbps"] = link;
        }
        env->DeleteLocalRef(info_cls);
      }
      if (info) env->DeleteLocalRef(info);
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(wm);
  }
  clear_exception(env.get(), "network_active[wifi]");

  return j;
}

nlohmann::json jni_recent_apps() {
  nlohmann::json j = nlohmann::json::array();
  Env env;
  if (!env) return j;

  jobject um = get_system_service(env.get(), "usage");
  if (!um) return j;

  struct AppEntry {
    std::string package;
    int64_t last_used_ms;
    int64_t total_ms;
  };

  jclass cls = env->GetObjectClass(um);
  jmethodID query = env->GetMethodID(
      cls, "queryUsageStats", "(IJJ)Ljava/util/List;");
  if (!query) {
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(um);
    return j;
  }

  jlong end = telemetry::now_ms();
  jlong start = end - 5LL * 60 * 1000;
  jobject list = env->CallObjectMethod(um, query, 4 /* INTERVAL_DAILY */, start, end);
  env->DeleteLocalRef(cls);
  env->DeleteLocalRef(um);
  if (!list || env->ExceptionCheck()) {
    clear_exception(env.get(), "recent_apps[query]");
    if (list) env->DeleteLocalRef(list);
    return j;
  }

  jclass list_cls = env->GetObjectClass(list);
  jmethodID size_m = env->GetMethodID(list_cls, "size", "()I");
  jmethodID get_m = env->GetMethodID(list_cls, "get", "(I)Ljava/lang/Object;");
  if (!size_m || !get_m) {
    env->DeleteLocalRef(list_cls);
    env->DeleteLocalRef(list);
    return j;
  }
  jint n = env->CallIntMethod(list, size_m);

  std::vector<AppEntry> apps;
  for (jint i = 0; i < n; ++i) {
    jobject us = env->CallObjectMethod(list, get_m, i);
    if (!us || env->ExceptionCheck()) {
      clear_exception(env.get(), "recent_apps[get]");
      continue;
    }
    jclass us_cls = env->GetObjectClass(us);
    jmethodID pkg = env->GetMethodID(us_cls, "getPackageName", "()Ljava/lang/String;");
    jmethodID last = env->GetMethodID(us_cls, "getLastTimeUsed", "()J");
    jmethodID total = env->GetMethodID(us_cls, "getTotalTimeInForeground", "()J");
    if (pkg && last && total) {
      jstring ps = (jstring)env->CallObjectMethod(us, pkg);
      jlong last_ms = env->CallLongMethod(us, last);
      jlong total_ms = env->CallLongMethod(us, total);
      if (!env->ExceptionCheck() && ps) {
        std::string p = to_utf(env.get(), ps);
        env->DeleteLocalRef(ps);
        if (p.compare(0, 17, "android:") != 0 && !p.empty()) {
          apps.push_back({p, last_ms, total_ms});
        }
      } else {
        clear_exception(env.get(), "recent_apps[stats]");
      }
    }
    env->DeleteLocalRef(us_cls);
    env->DeleteLocalRef(us);
  }
  env->DeleteLocalRef(list_cls);
  env->DeleteLocalRef(list);

  std::sort(apps.begin(), apps.end(), [](const AppEntry& a, const AppEntry& b) {
    return a.last_used_ms > b.last_used_ms;
  });
  for (size_t i = 0; i < apps.size() && i < 8; ++i) {
    j.push_back({{"package", apps[i].package},
                 {"last_used_ms", apps[i].last_used_ms},
                 {"total_fg_ms", apps[i].total_ms}});
  }
  return j;
}

}  // namespace telemetry::jni