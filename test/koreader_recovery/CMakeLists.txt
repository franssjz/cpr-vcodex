# Compile the production restart/validation functions with only hardware/store
# boundaries replaced. This exercises failed saves and unsupported boards too.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${REPO_ROOT}/src/main.cpp)
file(READ ${REPO_ROOT}/src/main.cpp main_source)
string(FIND "${main_source}" "static bool supportsKOReaderRecovery()" recovery_start)
string(FIND "${main_source}" "void toggleFrontlight()" recovery_end)
math(EXPR recovery_length "${recovery_end} - ${recovery_start}")
string(SUBSTRING "${main_source}" ${recovery_start} ${recovery_length} restart_methods)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/KOReaderRestart.inc "${restart_methods}")

add_executable(KOReaderRecoveryTest KOReaderRecoveryTest.cpp KOReaderRestartTest.cpp)
target_include_directories(KOReaderRecoveryTest PRIVATE ${REPO_ROOT}/src ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(KOReaderRecoveryTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(KOReaderRecoveryTest)

# Exercise the actual deferred initialization entrypoint: a network boot must
# install the resolver without loading any selected-font data.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${REPO_ROOT}/src/SdCardFontSystem.cpp ${REPO_ROOT}/src/SdCardFontSystem.h)
file(READ ${REPO_ROOT}/src/SdCardFontSystem.cpp font_system_source)
string(FIND "${font_system_source}" "void SdCardFontSystem::begin(" font_begin_start)
string(FIND "${font_system_source}" "void SdCardFontSystem::ensureLoaded(" font_begin_end)
math(EXPR font_begin_length "${font_begin_end} - ${font_begin_start}")
string(SUBSTRING "${font_system_source}" ${font_begin_start} ${font_begin_length} font_begin_method)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/SdCardFontBegin.inc "${font_begin_method}")
file(READ ${REPO_ROOT}/src/SdCardFontSystem.h font_system_header)
string(REGEX MATCH "void begin\\([^;]+;" font_begin_declaration "${font_system_header}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/SdCardFontBeginDecl.inc "${font_begin_declaration}")
add_executable(SdCardFontSystemBeginTest SdCardFontSystemBeginTest.cpp)
target_include_directories(SdCardFontSystemBeginTest PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(SdCardFontSystemBeginTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SdCardFontSystemBeginTest)
