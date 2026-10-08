# Exercise the production viewport method with the real SDK screen/list, without
# pulling in ESP32 activity lifecycle dependencies. Regenerate on source edits.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${REPO_ROOT}/src/activities/UiListActivity.cpp
  ${REPO_ROOT}/src/activities/UiListActivity.h)
file(READ ${REPO_ROOT}/src/activities/UiListActivity.cpp activity_source)
string(FIND "${activity_source}" "void UiListActivity::syncListViewport(" method_start)
string(FIND "${activity_source}" "void UiListActivity::drawChrome(" method_end)
math(EXPR method_length "${method_end} - ${method_start}")
string(SUBSTRING "${activity_source}" ${method_start} ${method_length} VIEWPORT_METHOD)
file(READ ${REPO_ROOT}/src/activities/UiListActivity.h activity_header)
string(REGEX MATCHALL "(void syncListViewport|int16_t measureActionListHeight)\\([^;]+" VIEWPORT_DECLARATIONS "${activity_header}")
string(JOIN ";\n" VIEWPORT_DECLARATIONS ${VIEWPORT_DECLARATIONS})
string(APPEND VIEWPORT_DECLARATIONS ";")
configure_file(ViewportAdapter.h.in ViewportAdapter.h @ONLY)

add_executable(ListNavigationTest ListNavigationTest.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp)
target_include_directories(ListNavigationTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include)
target_link_libraries(ListNavigationTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ListNavigationTest)
