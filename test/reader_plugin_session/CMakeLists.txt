add_executable(ReaderPluginSessionTest
  ReaderPluginSessionTest.cpp
  ${REPO_ROOT}/src/activities/reader/ReaderPluginSession.cpp
  ${REPO_ROOT}/src/activities/reader/ReaderSession.cpp
)
target_include_directories(ReaderPluginSessionTest PRIVATE
  stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/activities/reader
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/TrustedTime
)
target_link_libraries(ReaderPluginSessionTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderPluginSessionTest)
