add_executable(OpdsParserRecoveryTest
  OpdsParserRecoveryTest.cpp
  TrackedOpdsParser.cpp
)

target_include_directories(OpdsParserRecoveryTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/OpdsParser
  ${REPO_ROOT}/lib/XmlParserUtils
)

target_link_libraries(OpdsParserRecoveryTest PRIVATE
  crosspoint_test_common
  EXPAT::EXPAT
  GTest::gtest_main
)

gtest_discover_tests(OpdsParserRecoveryTest)
