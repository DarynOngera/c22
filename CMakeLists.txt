cmake_minimum_required(VERSION 3.22)
project(c22_telemetry LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

set(CORE_SOURCES
  src/core/buffer.cpp
  src/core/scheduler.cpp
  src/core/procfs.cpp
  src/core/engine.cpp
)
if(NOT ANDROID)
  list(APPEND CORE_SOURCES src/transport/null_client.cpp)
endif()

add_library(telemetry_core STATIC ${CORE_SOURCES})
target_include_directories(telemetry_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/include
)
target_link_libraries(telemetry_core PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# Android: shared library with JNI bridge + libcurl/OpenSSL transport
# ---------------------------------------------------------------------------
if(ANDROID)
  include(cmake/deps.cmake)

  # Ninja requires link inputs to exist when it loads the graph. Create empty
  # placeholders only when the real archives are not already present, so the
  # external projects overwrite them and a reconfigure never destroys a real
  # install. add_dependencies() ensures installs finish before linkage.
  file(MAKE_DIRECTORY ${DEPS_PREFIX}/lib)
  if(NOT EXISTS ${DEPS_PREFIX}/lib/libcurl.a)
    file(WRITE ${DEPS_PREFIX}/lib/libcurl.a "")
    file(WRITE ${DEPS_PREFIX}/lib/libssl.a "")
    file(WRITE ${DEPS_PREFIX}/lib/libcrypto.a "")
  endif()

  add_library(telemetry SHARED
    src/jni/jni_util.cpp
    src/jni/android_context.cpp
    src/jni/native_bridge.cpp
    src/transport/curl_client.cpp
  )
  set_target_properties(telemetry PROPERTIES OUTPUT_NAME telemetry)

  target_include_directories(telemetry PRIVATE ${CURL_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR})
  target_link_libraries(telemetry PRIVATE
    telemetry_core
    ${CURL_ARCHIVE}
    ${OPENSSL_SSL_ARCHIVE}
    ${OPENSSL_CRYPTO_ARCHIVE}
    log
  )
  add_dependencies(telemetry curl_export)
endif()

# ---------------------------------------------------------------------------
# Host unit tests
# ---------------------------------------------------------------------------
if(TELEMETRY_BUILD_TESTS AND NOT ANDROID)
  enable_testing()
  add_executable(telemetry_tests
    tests/test_main.cpp
    tests/buffer_test.cpp
    tests/json_test.cpp
    tests/procfs_test.cpp
    tests/engine_test.cpp
  )
  target_link_libraries(telemetry_tests PRIVATE telemetry_core)
  add_test(NAME telemetry_core COMMAND telemetry_tests)
endif()