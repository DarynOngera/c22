# Bundles OpenSSL + libcurl for the Android target using the NDK toolchain.
# Produces static archives consumed by libtelemetry.so.
# Include only when ANDROID is set.

include(ExternalProject)

set(DEPS_JOBS 4 CACHE STRING "Parallel jobs for third-party builds")

set(DEPS_PREFIX "${CMAKE_BINARY_DIR}/third_party/install")
set(NDK_LLVM_BIN "${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin")

find_program(PERL perl)
if(NOT PERL)
  message(FATAL_ERROR "OpenSSL needs perl to configure")
endif()

ExternalProject_Add(openssl_export
  PREFIX "${CMAKE_BINARY_DIR}/third_party/openssl"
  URL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/downloads/openssl.tar.gz"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  DOWNLOAD_NO_PROGRESS TRUE
  BYPRODUCTS
    "${DEPS_PREFIX}/lib/libssl.a"
    "${DEPS_PREFIX}/lib/libcrypto.a"
  BUILD_IN_SOURCE 1
  CONFIGURE_COMMAND
    "${CMAKE_COMMAND}" -E env
    "ANDROID_NDK_ROOT=${ANDROID_NDK}"
    "PATH=${NDK_LLVM_BIN}:$ENV{PATH}"
    perl Configure android-arm64 -D__ANDROID_API__=29 no-shared no-tests
    "--prefix=${DEPS_PREFIX}"
  BUILD_COMMAND
    "${CMAKE_COMMAND}" -E env "PATH=${NDK_LLVM_BIN}:$ENV{PATH}" make -j${DEPS_JOBS}
  INSTALL_COMMAND
    "${CMAKE_COMMAND}" -E env "PATH=${NDK_LLVM_BIN}:$ENV{PATH}" make install_sw
  LOG_CONFIGURE 1 LOG_BUILD 1 LOG_INSTALL 1
)

ExternalProject_Add(curl_export
  PREFIX "${CMAKE_BINARY_DIR}/third_party/curl"
  URL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/downloads/curl.tar.gz"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  DOWNLOAD_NO_PROGRESS TRUE
  BYPRODUCTS "${DEPS_PREFIX}/lib/libcurl.a"
  DEPENDS openssl_export
  CMAKE_ARGS
    -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK}/build/cmake/android.toolchain.cmake
    -DANDROID_ABI=${ANDROID_ABI}
    -DANDROID_PLATFORM=android-29
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=${DEPS_PREFIX}
    -DCMAKE_PREFIX_PATH=${DEPS_PREFIX}
    -DCMAKE_FIND_ROOT_PATH=${DEPS_PREFIX}
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_CURL_EXE=OFF
    -DBUILD_TESTING=OFF
    -DHTTP_ONLY=ON
    -DCURL_USE_OPENSSL=ON
    -DOPENSSL_ROOT_DIR=${DEPS_PREFIX}
    -DOPENSSL_USE_STATIC_LIBS=ON
    -DCURL_ZLIB=OFF
    -DCURL_USE_LIBSSH2=OFF
    -DCURL_DISABLE_LDAP=ON
    -DCURL_DISABLE_LDAPS=ON
    -DCURL_DISABLE_RTSP=ON
    -DCURL_DISABLE_TELNET=ON
    -DCURL_DISABLE_TFTP=ON
    -DCURL_DISABLE_GOPHER=ON
    -DCURL_DISABLE_SMB=ON
    -DCURL_DISABLE_DICT=ON
    -DCURL_DISABLE_IMAP=ON
    -DCURL_DISABLE_POP3=ON
  LOG_CONFIGURE 1 LOG_BUILD 1 LOG_INSTALL 1
)

set(OPENSSL_INCLUDE_DIR "${DEPS_PREFIX}/include")
set(CURL_INCLUDE_DIR "${DEPS_PREFIX}/include")
set(OPENSSL_SSL_ARCHIVE "${DEPS_PREFIX}/lib/libssl.a")
set(OPENSSL_CRYPTO_ARCHIVE "${DEPS_PREFIX}/lib/libcrypto.a")
set(CURL_ARCHIVE "${DEPS_PREFIX}/lib/libcurl.a")