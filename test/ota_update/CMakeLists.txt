add_executable(OtaUpdateTest
  OtaUpdateTest.cpp
  ${REPO_ROOT}/src/network/OtaUpdater.cpp
  ${REPO_ROOT}/src/network/FirmwareBoardTag.cpp
  ${REPO_ROOT}/lib/JsonParser/FirmwareManifestJsonParser.cpp
  ${REPO_ROOT}/lib/JsonParser/ReleaseJsonParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)
target_compile_definitions(OtaUpdateTest PRIVATE FREEINK_DEVICE_X4=1 TEST_REPO_ROOT="${REPO_ROOT}")
target_include_directories(OtaUpdateTest PRIVATE
  stubs ${REPO_ROOT}/src ${REPO_ROOT}/src/network ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/test/reader_regressions/fakes
)
target_link_libraries(OtaUpdateTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(OtaUpdateTest)
