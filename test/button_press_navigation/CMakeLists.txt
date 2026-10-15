add_executable(ButtonPressNavigationTest ButtonPressNavigationTest.cpp ../../src/util/ButtonNavigator.cpp)
target_include_directories(ButtonPressNavigationTest PRIVATE stubs ../../src)
target_link_libraries(ButtonPressNavigationTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ButtonPressNavigationTest)

# Exercise production input sampling/hold methods without board dependencies.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${REPO_ROOT}/src/MappedInputManager.cpp ${REPO_ROOT}/src/MappedInputManager.h)
file(READ ${REPO_ROOT}/src/MappedInputManager.cpp mapped_source)
function(extract_mapped_method first next output)
  string(FIND "${mapped_source}" "${first}" start)
  string(FIND "${mapped_source}" "${next}" end)
  math(EXPR length "${end} - ${start}")
  string(SUBSTRING "${mapped_source}" ${start} ${length} method)
  set(${output} "${method}" PARENT_SCOPE)
endfunction()
extract_mapped_method("void MappedInputManager::update(" "bool MappedInputManager::isNavDirectionSwapped("
  MAPPED_UPDATE_METHOD)
extract_mapped_method("bool MappedInputManager::wasLongPressed(" "void MappedInputManager::suppressNextRelease("
  MAPPED_LONG_PRESS_METHOD)
extract_mapped_method("unsigned long MappedInputManager::getHeldTime("
  "MappedInputManager::Labels MappedInputManager::mapLabels(" MAPPED_HELD_TIME_METHOD)
string(REGEX MATCH "constexpr unsigned long TOUCH_HELD_OVERRIDE_WINDOW_MS[^;]+;" TOUCH_OVERRIDE_CONSTANT
  "${mapped_source}")
file(READ ${REPO_ROOT}/src/MappedInputManager.h mapped_header)
string(REGEX MATCH "enum class Button \\{[^}]+\\};" MAPPED_BUTTONS "${mapped_header}")
string(REGEX MATCH "mutable unsigned long sampledButtonHeldMs[^;]+;" MAPPED_HELD_FIELD "${mapped_header}")
configure_file(MappedHeldTimeAdapter.h.in MappedHeldTimeAdapter.h @ONLY)
add_executable(MappedHeldTimeTest MappedHeldTimeTest.cpp)
target_include_directories(MappedHeldTimeTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ../../src)
target_link_libraries(MappedHeldTimeTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(MappedHeldTimeTest)
