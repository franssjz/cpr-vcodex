find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(ACTIVITY_MANAGER_METHODS ${CMAKE_CURRENT_BINARY_DIR}/ActivityManagerMethods.inc)
add_custom_command(
  OUTPUT ${ACTIVITY_MANAGER_METHODS}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_methods.py
          ${REPO_ROOT}/src/activities/ActivityManager.cpp ${ACTIVITY_MANAGER_METHODS}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/extract_methods.py ${REPO_ROOT}/src/activities/ActivityManager.cpp
  VERBATIM
)
add_executable(ActivityLifecycleTest ActivityLifecycleTest.cpp ${ACTIVITY_MANAGER_METHODS})
target_include_directories(ActivityLifecycleTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(ActivityLifecycleTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ActivityLifecycleTest)
