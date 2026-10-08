set(OTA_TEST_SOURCES
  OtaUpdateTest.cpp
  ${REPO_ROOT}/src/network/OtaUpdater.cpp
  ${REPO_ROOT}/src/network/FirmwareBoardTag.cpp
  ${REPO_ROOT}/lib/JsonParser/FirmwareManifestJsonParser.cpp
  ${REPO_ROOT}/lib/JsonParser/ReleaseJsonParser.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp
)
foreach(target OtaUpdateTest OtaUpdateX3Test OtaUpdateDevTest)
  add_executable(${target} ${OTA_TEST_SOURCES})
  target_compile_definitions(${target} PRIVATE TEST_REPO_ROOT="${REPO_ROOT}")
  target_include_directories(${target} PRIVATE
    stubs ${REPO_ROOT}/src ${REPO_ROOT}/src/network ${REPO_ROOT}/lib/JsonParser
    ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include
    ${REPO_ROOT}/test/reader_regressions/fakes
  )
  target_link_libraries(${target} PRIVATE crosspoint_test_common GTest::gtest_main)
  gtest_discover_tests(${target} TEST_PREFIX "${target}.")
endforeach()

target_compile_definitions(OtaUpdateTest PRIVATE FREEINK_DEVICE_X4=1)
target_compile_definitions(OtaUpdateX3Test PRIVATE FREEINK_DEVICE_X3=1)
target_compile_definitions(OtaUpdateDevTest PRIVATE FREEINK_DEVICE_X4=1 FREEINK_DEVICE_X3=1
  TEST_INSTALLED_VERSION="1.6.0.38.dev35-a4ae01a7")
