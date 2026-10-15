# Build the complete production client with real JSON and host transport doubles.
find_path(KOREADER_JSON_INCLUDE ArduinoJson.h
  HINTS ${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src
        ${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src)
if(NOT KOREADER_JSON_INCLUDE)
  FetchContent_Declare(koreader_arduinojson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.2)
  FetchContent_MakeAvailable(koreader_arduinojson)
  set(KOREADER_JSON_INCLUDE ${koreader_arduinojson_SOURCE_DIR}/src)
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${REPO_ROOT}/lib/KOReaderSync/KOReaderSyncClient.cpp)
file(READ ${REPO_ROOT}/lib/KOReaderSync/KOReaderSyncClient.cpp CLIENT_SOURCE)
string(REGEX REPLACE "#include [^\n]*\n" "" CLIENT_SOURCE "${CLIENT_SOURCE}")
configure_file(KOReaderSyncAdapter.h.in KOReaderSyncAdapter.h @ONLY)
add_executable(KOReaderSyncClientTest KOReaderSyncClientTest.cpp)
target_include_directories(KOReaderSyncClientTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
  ${KOREADER_JSON_INCLUDE} ${REPO_ROOT}/lib/KOReaderSync)
target_link_libraries(KOReaderSyncClientTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(KOReaderSyncClientTest)
