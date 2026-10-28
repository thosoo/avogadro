# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/thaison/avogadro/extern/openbabel"
  "/home/thaison/avogadro/build-asan/openbabel_ext-prefix/src/openbabel_ext-build"
  "/home/thaison/avogadro/build-asan/openbabel_ext-prefix"
  "/home/thaison/avogadro/build-asan/openbabel_ext-prefix/tmp"
  "/home/thaison/avogadro/build-asan/openbabel_ext-prefix/src/openbabel_ext-stamp"
  "/home/thaison/avogadro/build-asan/openbabel_ext-prefix/src"
  "/home/thaison/avogadro/build-asan/openbabel_ext-prefix/src/openbabel_ext-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/thaison/avogadro/build-asan/openbabel_ext-prefix/src/openbabel_ext-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/thaison/avogadro/build-asan/openbabel_ext-prefix/src/openbabel_ext-stamp${cfgdir}") # cfgdir has leading slash
endif()
