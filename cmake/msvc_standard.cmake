# CMake 4.4.4 still maps CXX_STANDARD 23 to c++latest. Override before feature probes.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "19.51.36260")
    set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "-std:c++23preview")
    set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "-std:c++23preview")
else()
    message(FATAL_ERROR "Unvalidated compiler: revalidate the named C++23 mode before updating the toolchain pin")
endif()
