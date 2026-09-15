add_executable(DifferentialRoundingTest
  DifferentialRoundingTest.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(DifferentialRoundingTest PRIVATE
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(DifferentialRoundingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DifferentialRoundingTest)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/builtin_kerning_cases.generated.h
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/GenerateBuiltinKerningCases.py
          ${CMAKE_CURRENT_BINARY_DIR}/builtin_kerning_cases.generated.h
  DEPENDS GenerateBuiltinKerningCases.py builtin_kerning_golden.json
  VERBATIM
)
add_executable(BuiltinKerningRuntimeTest
  BuiltinKerningRuntimeTest.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/builtin_kerning_cases.generated.h
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(BuiltinKerningRuntimeTest PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Utf8)
target_link_libraries(BuiltinKerningRuntimeTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BuiltinKerningRuntimeTest)
