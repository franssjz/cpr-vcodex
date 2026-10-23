find_path(STABLE_PAGES_JSON_INCLUDE ArduinoJson.h
  HINTS ${REPO_ROOT}/.pio/libdeps/default/ArduinoJson/src
        ${REPO_ROOT}/.pio/libdeps/gh_release/ArduinoJson/src)
if(NOT STABLE_PAGES_JSON_INCLUDE)
  include(FetchContent)
  FetchContent_Declare(stable_pages_arduinojson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG v7.4.2)
  FetchContent_MakeAvailable(stable_pages_arduinojson)
  set(STABLE_PAGES_JSON_INCLUDE ${stable_pages_arduinojson_SOURCE_DIR}/src)
endif()
# Epub.h exposes reference-page types, so parser suites that include Epub.h
# also need the real ArduinoJson headers. Share them through the test interface.
target_include_directories(crosspoint_test_common INTERFACE ${STABLE_PAGES_JSON_INCLUDE})
add_executable(StablePageNumbersTest StablePageNumbersTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/StablePageNumbers.cpp)
target_include_directories(StablePageNumbersTest PRIVATE ${STABLE_PAGES_JSON_INCLUDE} ${REPO_ROOT}/lib/Epub/Epub)
target_link_libraries(StablePageNumbersTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(StablePageNumbersTest)
