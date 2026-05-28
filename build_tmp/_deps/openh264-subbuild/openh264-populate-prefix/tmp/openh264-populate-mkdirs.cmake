# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "D:/AAA_C/compression-tool/build_tmp/_deps/openh264-src")
  file(MAKE_DIRECTORY "D:/AAA_C/compression-tool/build_tmp/_deps/openh264-src")
endif()
file(MAKE_DIRECTORY
  "D:/AAA_C/compression-tool/build_tmp/_deps/openh264-build"
  "D:/AAA_C/compression-tool/build_tmp/_deps/openh264-subbuild/openh264-populate-prefix"
  "D:/AAA_C/compression-tool/build_tmp/_deps/openh264-subbuild/openh264-populate-prefix/tmp"
  "D:/AAA_C/compression-tool/build_tmp/_deps/openh264-subbuild/openh264-populate-prefix/src/openh264-populate-stamp"
  "D:/AAA_C/compression-tool/build_tmp/_deps/openh264-subbuild/openh264-populate-prefix/src"
  "D:/AAA_C/compression-tool/build_tmp/_deps/openh264-subbuild/openh264-populate-prefix/src/openh264-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/AAA_C/compression-tool/build_tmp/_deps/openh264-subbuild/openh264-populate-prefix/src/openh264-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/AAA_C/compression-tool/build_tmp/_deps/openh264-subbuild/openh264-populate-prefix/src/openh264-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
