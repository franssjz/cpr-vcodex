# Compile the production activity with lightweight host platform doubles and
# the real SDK list layout/navigation, following test/list_navigation's adapter.
set(activity_path ${REPO_ROOT}/src/activities/reader/BookmarksActivity)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${activity_path}.cpp ${activity_path}.h
  ${REPO_ROOT}/src/activities/reader/BookmarkStore.h
  ${REPO_ROOT}/src/activities/UiListActivity.cpp)
file(READ ${activity_path}.h BOOKMARKS_HEADER)
string(REGEX REPLACE "#include [^\n]*\n" "" BOOKMARKS_HEADER "${BOOKMARKS_HEADER}")
string(REPLACE "class BookmarksActivity final : public UiListActivity {"
  "class BookmarksActivity final : public UiListActivity {\n public:"
  BOOKMARKS_HEADER "${BOOKMARKS_HEADER}")
file(READ ${activity_path}.cpp activity_source)
string(FIND "${activity_source}" "namespace fui =" method_start)
string(SUBSTRING "${activity_source}" ${method_start} -1 BOOKMARKS_METHODS)
file(READ ${REPO_ROOT}/src/activities/reader/BookmarkStore.h store_header)
string(REGEX MATCH "struct Bookmark \\{[^}]+\\};" BOOKMARK_MODEL "${store_header}")
file(READ ${REPO_ROOT}/src/activities/UiListActivity.cpp list_source)
string(FIND "${list_source}" "void UiListActivity::syncListViewport(" method_start)
string(FIND "${list_source}" "int16_t UiListActivity::measureActionListHeight(" method_end)
math(EXPR method_length "${method_end} - ${method_start}")
string(SUBSTRING "${list_source}" ${method_start} ${method_length} VIEWPORT_METHOD)
configure_file(BookmarksAdapter.h.in BookmarksAdapter.h @ONLY)

add_executable(BookmarksWindowTest BookmarksWindowTest.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp)
target_include_directories(BookmarksWindowTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include)
target_link_libraries(BookmarksWindowTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookmarksWindowTest)
