project(runguard)
cmake_minimum_required(VERSION 3.9.4)
include(ExternalProject)

if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8.0)
  message(FATAL_ERROR "Insufficient gcc version, need 8.0 or higher")
endif()

set(default_build_type "Debug")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

set(BUILD_TARGET runguard)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

# header directories
################################################################################
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
################################################################################

# source files
################################################################################
file(GLOB_RECURSE SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER SOURCE_FILES EXCLUDE REGEX ".*runguard.cpp$")
file(GLOB ENTRY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/runguard.cpp")
################################################################################

# Necessary libraries
################################################################################
find_package(Threads REQUIRED)
find_package(Boost 1.65 REQUIRED COMPONENTS program_options log)

include(ProcessorCount)
ProcessorCount(N)
ExternalProject_Add(libcgroup_src
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}
    URL https://github.com/libcgroup/libcgroup/releases/download/v2.0/libcgroup-2.0.tar.gz
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/libcgroup
    SOURCE_DIR ${libcgroup_SOURCE_DIR}
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ./configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/libcgroup --enable-pam=no --enable-shared=no
    BUILD_COMMAND make -j${N}
    INSTALL_COMMAND make install
    BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/libcgroup/lib/libcgroup.a"
)

ExternalProject_Add(libseccomp_src
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}
    URL https://github.com/seccomp/libseccomp/releases/download/v2.5.2/libseccomp-2.5.2.tar.gz
    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/libseccomp
    SOURCE_DIR ${libseccomp_SOURCE_DIR}
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ./configure --prefix=${CMAKE_CURRENT_BINARY_DIR}/libseccomp --enable-shared=no
    BUILD_COMMAND make -j${N}
    INSTALL_COMMAND make install
    BUILD_BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/libseccomp/lib/libseccomp.a"
)

add_library(libcgroup STATIC IMPORTED GLOBAL)
set_property(TARGET libcgroup
    PROPERTY IMPORTED_LOCATION 
    "${CMAKE_CURRENT_BINARY_DIR}/libcgroup/lib/libcgroup.a"
)

add_library(libseccomp STATIC IMPORTED GLOBAL)
set_property(TARGET libseccomp
    PROPERTY IMPORTED_LOCATION 
    "${CMAKE_CURRENT_BINARY_DIR}/libseccomp/lib/libseccomp.a"
)
################################################################################
include_directories("${CMAKE_CURRENT_BINARY_DIR}/libseccomp/include")
include_directories("${CMAKE_CURRENT_BINARY_DIR}/libcgroup/include")

add_executable(${BUILD_TARGET} ${SOURCE_FILES} ${ENTRY_FILE})
add_dependencies(${BUILD_TARGET} libcgroup_src libseccomp_src)
set_target_properties(${BUILD_TARGET}
  PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
  CXX_STANDARD 17
)
target_link_libraries(${BUILD_TARGET}
  fmt

  libcgroup
  libseccomp
  
  ${Boost_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
  -static
)

install(TARGETS ${BUILD_TARGET} RUNTIME DESTINATION runguard/bin COMPONENT runtime)
