#include "jni/jni_util.h"

#include <pthread.h>

#include <android/log.h>

namespace telemetry::jni {
namespace {

JavaVM* g_jvm = nullptr;
pthread_key_t g_env_key;
pthread_once_t g_once = PTHREAD_ONCE_INIT;

void on_thread_destroy(void*) {
  if (g_jvm) g_jvm->DetachCurrentThread();
}

void make_key() { pthread_key_create(&g_env_key, on_thread_destroy); }

void ensure_key() { pthread_once(&g_once, make_key); }

}  // namespace

void init_jvm(JavaVM* vm) {
  g_jvm = vm;
  make_key();
}

JavaVM* jvm() { return g_jvm; }

Env::Env() {
  if (!g_jvm) return;
  JNIEnv* env = nullptr;
  jint rc = g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  if (rc == JNI_EDETACHED) {
    // Attach and track the thread so we can detach when the thread exits.
    ensure_key();
    JavaVMAttachArgs args{JNI_VERSION_1_6, nullptr, nullptr};
    if (g_jvm->AttachCurrentThread(&env, &args) == JNI_OK) {
      attached_ = true;
      pthread_setspecific(g_env_key, reinterpret_cast<void*>(1));
    } else {
      return;
    }
  } else if (rc != JNI_OK) {
    return;
  }
  env_ = env;
}

Env::~Env() {
  if (attached_ && g_jvm) {
    // Clear the TLS flag so the thread-exit destructor does not detach a
    // thread we already detached here (double DetachCurrentThread is UB).
    pthread_setspecific(g_env_key, nullptr);
    g_jvm->DetachCurrentThread();
    attached_ = false;
  }
}

std::string to_utf(JNIEnv* env, jstring s) {
  if (!env || !s) return {};
  const char* utf = env->GetStringUTFChars(s, nullptr);
  if (!utf) return {};
  std::string out(utf);
  env->ReleaseStringUTFChars(s, utf);
  return out;
}

bool clear_exception(JNIEnv* env, const char* where) {
  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    __android_log_print(ANDROID_LOG_WARN, "telemetry", "caught Java exception at %s", where);
    return true;
  }
  return false;
}

}  // namespace telemetry::jni