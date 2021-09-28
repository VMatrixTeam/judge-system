cmake_minimum_required(VERSION 3.12)
project(judge-system
        VERSION 0.1.0
        DESCRIPTION "Matrix judge system"
        LANGUAGES C CXX
        )
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

set(MATRIX_JUDGE_TARGET judge-system)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -no-pie -fno-pie")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib" isSystemDir)
if("${isSystemDir}" STREQUAL "-1")
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
endif("${isSystemDir}" STREQUAL "-1")
IF(WIN32)
    SET(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
ELSE(WIN32)
    SET(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
ENDIF(WIN32)

add_definitions(-DORMPP_ENABLE_MYSQL -DBOOST_STACKTRACE_USE_ADDR2LINE)

#  CMake control options
################################################################################
option(BUILD_UNIT_TEST "Build the unit test library" OFF)
option(BUILD_GTEST_MODULE_TEST "Build test for gtest module" OFF)

option(BUILD_ENTRY "Build the Judge System main entry" OFF)
################################################################################

# Necessary libraries
################################################################################
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(Boost 1.65 REQUIRED COMPONENTS log_setup log program_options thread)
find_package(Protobuf REQUIRED)

pkg_check_modules(MySQL REQUIRED IMPORTED_TARGET mysqlclient)

# header directories
################################################################################
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/ext/cpr/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/ext/fmt/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/ext/SimpleAmqpClient/src")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/ext/prometheus-cpp/pull/include")
include_directories("${Protobuf_INCLUDE_DIRS}")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")
################################################################################

# Protobuf codegen
################################################################################
file(GLOB protobuf_files
     include/server/proto/*.proto)
SET(PROTO_META_BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
LIST(APPEND PROTO_FLAGS "-l${CMAKE_CURRENT_SOURCE_DIR}")

FOREACH(proto_file ${protobuf_files})
  STRING(REGEX REPLACE "[^/]proto/" "" proto_file_name ${proto_file})
  LIST(APPEND PROTO_SRCS "${proto_file_name}.pb.cc")
	ADD_CUSTOM_COMMAND(
		OUTPUT "${proto_file_name}.pb.h" "${proto_file_name}.pb.cc"
		COMMAND protoc --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
                       --cpp_out=${CMAKEs_CURRENT_SOURCE_DIR}/proto/ ${proto_file}
		DEPENDS ${proto_file}
	)
ENDFOREACH()
message(STATUS "Generated proto sources ${PROTO_SRCS}")
################################################################################

# source files
################################################################################
file(GLOB_RECURSE SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER SOURCE_FILES EXCLUDE REGEX ".*main.cpp$")
file(GLOB ENTRY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
################################################################################

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/ext/fmt")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/ext/SimpleAmqpClient")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/ext/prometheus-cpp")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/ext/cpr")

if (BUILD_UNIT_TEST OR BUILD_GTEST_MODULE_TEST)
  if (NOT TARGET gtest)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/ext/googletest")
  endif ()
endif ()
################################################################################

if (BUILD_UNIT_TEST)
  # Unit test header files
  ################################################################################
  include_directories("${CMAKE_CURRENT_SOURCE_DIR}/ext/googletest/googletest/include")
  include_directories("${CMAKE_CURRENT_SOURCE_DIR}/ext/googlemock/googlemock/include")
  include_directories("${CMAKE_CURRENT_SOURCE_DIR}/unit-test/")
  ################################################################################

  # Unit test source files
  ################################################################################
  file(GLOB_RECURSE TEST_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/unit-test/*Test.cpp")
  file(GLOB TEST_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/unit-test/main.cpp")
  ################################################################################

  set(GTEST_TARGET "unit_test")
  add_executable(${GTEST_TARGET} ${TEST_SOURCE_FILES} ${TEST_MAIN} ${SOURCE_FILES})
  set_target_properties(${GTEST_TARGET}
    PROPERTIES
    CXX_STANDARD 17)
  target_link_libraries(${GTEST_TARGET}
    # TODO: add depended libraries
    gmock
    SimpleAmqpClient
    fmt
    boost_stacktrace_addr2line
    dl
    cpr
    stdc++fs

    PkgConfig::MySQL

    ${Boost_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    )
endif ()


if (BUILD_GTEST_MODULE_TEST)
  file(GLOB GTEST_TEST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/test/main.cpp")
  set(GTEST_TEST_TARGET "gtest_test")
  add_executable(${GTEST_TEST_TARGET} ${GTEST_TEST_FILE})
  set_target_properties(${GTEST_TEST_TARGET}
    PROPERTIES
    CXX_STANDARD 17
    )
  target_link_libraries(${GTEST_TEST_TARGET}
    ${CMAKE_THREAD_LIBS_INIT}
    gmock
    )
endif ()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/runguard")
add_dependencies(runguard fmt)

add_executable(${MATRIX_JUDGE_TARGET} ${SOURCE_FILES} ${ENTRY_FILE})
set_target_properties(${MATRIX_JUDGE_TARGET}
  PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
  CXX_STANDARD 17
)

if (WITH_ADDRESS_SANITIZER)
  target_compile_options(${MATRIX_JUDGE_TARGET} PUBLIC -fno-omit-frame-pointer PUBLIC -fsanitize=address)
  target_link_options(${MATRIX_JUDGE_TARGET} PUBLIC -fno-omit-frame-pointer PUBLIC -fsanitize=address)
endif ()

target_link_libraries(${MATRIX_JUDGE_TARGET}
  PRIVATE fmt
  PRIVATE SimpleAmqpClient
  PRIVATE boost_stacktrace_addr2line
  PRIVATE dl
  PRIVATE stdc++fs
  PRIVATE prometheus-cpp::pull
  PRIVATE PkgConfig::MySQL
  PRIVATE cpr  

  PRIVATE ${Boost_LIBRARIES}

  PRIVATE ${CMAKE_THREAD_LIBS_INIT}
  -static
)

install(TARGETS ${MATRIX_JUDGE_TARGET} DESTINATION bin)
install(DIRECTORY script/ DESTINATION script)
install(DIRECTORY exec/ DESTINATION exec FILE_PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_READ WORLD_EXECUTE)
install(PROGRAMS run.sh DESTINATION ./)
install(PROGRAMS prepare.sh DESTINATION ./)
