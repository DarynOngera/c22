#include <jni.h>

#include "core/engine.h"
#include "jni/android_context.h"
#include "jni/jni_util.h"

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
  telemetry::jni::init_jvm(vm);
  (void)reserved;
  return JNI_VERSION_1_6;
}

extern "C" {

JNIEXPORT void JNICALL Java_com_telemetry_Telemetry_attach(
    JNIEnv* env, jclass, jobject context, jstring device_id, jstring endpoint) {
  std::string did = telemetry::jni::to_utf(env, device_id);
  std::string ep = telemetry::jni::to_utf(env, endpoint);
  telemetry::jni::set_android_context(env, context);
  telemetry::Engine::instance().set_device_id(did);
  if (!ep.empty()) telemetry::Engine::instance().set_endpoint(ep);
}

JNIEXPORT void JNICALL Java_com_telemetry_Telemetry_start(JNIEnv*, jclass) {
  telemetry::Engine::instance().start();
}

JNIEXPORT void JNICALL Java_com_telemetry_Telemetry_stop(JNIEnv*, jclass) {
  telemetry::Engine::instance().stop();
}

JNIEXPORT void JNICALL Java_com_telemetry_Telemetry_flush(JNIEnv*, jclass) {
  telemetry::Engine::instance().flush_all();
}

JNIEXPORT void JNICALL Java_com_telemetry_Telemetry_locationUpdate(
    JNIEnv* env, jclass, jdouble lat, jdouble lng, jfloat accuracy, jstring provider,
    jlong fix_ms) {
  telemetry::Engine::instance().emit(
      "location",
      {{"lat", lat},
       {"lng", lng},
       {"accuracy_m", static_cast<double>(accuracy)},
       {"provider", telemetry::jni::to_utf(env, provider)},
       {"fix_ms", static_cast<int64_t>(fix_ms)}});
}

}  // extern "C"