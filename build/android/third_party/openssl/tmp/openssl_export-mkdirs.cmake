# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/ongera/projects/c22/build/android/third_party/openssl/src/openssl_export")
  file(MAKE_DIRECTORY "/home/ongera/projects/c22/build/android/third_party/openssl/src/openssl_export")
endif()
file(MAKE_DIRECTORY
  "/home/ongera/projects/c22/build/android/third_party/openssl/src/openssl_export-build"
  "/home/ongera/projects/c22/build/android/third_party/openssl"
  "/home/ongera/projects/c22/build/android/third_party/openssl/tmp"
  "/home/ongera/projects/c22/build/android/third_party/openssl/src/openssl_export-stamp"
  "/home/ongera/projects/c22/build/android/third_party/openssl/src"
  "/home/ongera/projects/c22/build/android/third_party/openssl/src/openssl_export-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/ongera/projects/c22/build/android/third_party/openssl/src/openssl_export-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/ongera/projects/c22/build/android/third_party/openssl/src/openssl_export-stamp${cfgdir}") # cfgdir has leading slash
endif()
