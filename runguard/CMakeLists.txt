project(runguard)
cmake_minimum_required(VERSION 3.9.4)

if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8.0)
  message(FATAL_ERROR "Insufficient gcc version, need 8.0 or higher")
endif()

set(CMAKE_BUILD_TYPE Debug)
# set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

set(BUILD_TARGET runguard)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

# header directories
################################################################################
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../ext/fmt/include")
################################################################################

# source files
################################################################################
file(GLOB_RECURSE SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER SOURCE_FILES EXCLUDE REGEX ".*runguard.cpp$")
file(GLOB ENTRY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/runguard.cpp")
################################################################################

# Necessary libraries
################################################################################
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(Boost 1.65 REQUIRED COMPONENTS program_options log)

pkg_check_modules(Cgroup REQUIRED IMPORTED_TARGET libcgroup)
pkg_check_modules(Seccomp REQUIRED IMPORTED_TARGET libseccomp)

################################################################################

add_executable(${BUILD_TARGET} ${SOURCE_FILES} ${ENTRY_FILE})
set_target_properties(${BUILD_TARGET}
  PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin"
  CXX_STANDARD 17
)
target_link_libraries(${BUILD_TARGET}
  fmt

  PkgConfig::Cgroup
  PkgConfig::Seccomp
  
  ${Boost_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
)
install(TARGETS ${BUILD_TARGET} RUNTIME DESTINATION bin)
