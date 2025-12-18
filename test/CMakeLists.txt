
# Fetch Catch2
Include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.2.1 # or a later release
)

FetchContent_MakeAvailable(Catch2)

#set (TEST_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
#file (GLOB_RECURSE TEST_SOURCE_FILES "${TEST_MODULE_PATH}/*.cpp")
#include_directories(${UTILS_INCLUDE_PATH} ${PARSER_INCLUDE_PATH} ${SYNTHESIS_INCLUDE_PATH} ${EXT_INCLUDE_PATH})
#add_executable(tests ${TEST_SOURCE_FILES} utils.hpp integration_tests_utils.hpp
#        test_ltlfplus_synthesis.cpp test_ppltlfplus_synthesis.cpp)
#target_link_libraries(tests PRIVATE Catch2::Catch2WithMain ${PARSER_LIB_NAME} ${SYNTHESIS_LIB_NAME} ${UTILS_LIB_NAME} ${EXT_LIBRARIES_PATH})
set(TEST_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
# Only include .cpp files directly in the test directory, not in subdirectories
# This avoids picking up CMake-generated files in build/ and CMakeFiles/ directories
file(GLOB TEST_SOURCE_FILES CONFIGURE_DEPENDS
    "${TEST_MODULE_PATH}/*.cpp"
)

add_executable(tests ${TEST_SOURCE_FILES})
target_include_directories(tests PRIVATE ${UTILS_INCLUDE_PATH} ${PARSER_INCLUDE_PATH} ${SYNTHESIS_INCLUDE_PATH} ${EXT_INCLUDE_PATH})
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain ${PARSER_LIB_NAME} ${SYNTHESIS_LIB_NAME} ${UTILS_LIB_NAME} ${LYDIA_LIBRARIES})

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)

catch_discover_tests(tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME tests COMMAND tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

