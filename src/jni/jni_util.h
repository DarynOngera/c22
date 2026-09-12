#pragma once

#include <jni.h>

#include <string>

namespace telemetry::jni {

void init_jvm(JavaVM* vm);
JavaVM* jvm();

// RAII that returns the current thread's JNIEnv, attaching the thread if needed.
// Detaches on destruction only if it performed the attachment.
class Env {
 public:
  Env();
  ~Env();
  Env(const Env&) = delete;
  Env& operator=(const Env&) = delete;

  JNIEnv* get() const { return env_; }
  JNIEnv* operator->() const { return env_; }
  explicit operator bool() const { return env_ != nullptr; }

 private:
  JNIEnv* env_ = nullptr;
  bool attached_ = false;
};

std::string to_utf(JNIEnv* env, jstring s);

// Logs and clears a pending Java exception. Returns true if one occurred.
bool clear_exception(JNIEnv* env, const char* where);

}  // namespace telemetry::jni