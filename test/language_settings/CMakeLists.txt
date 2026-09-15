find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(I18N_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${I18N_TEST_DIR}")
# Isolate generated tables from simultaneous PlatformIO builds.
configure_file(${REPO_ROOT}/lib/I18n/I18n.cpp ${I18N_TEST_DIR}/I18n.cpp COPYONLY)
configure_file(${REPO_ROOT}/lib/I18n/I18n.h ${I18N_TEST_DIR}/I18n.h COPYONLY)
file(GLOB I18N_TRANSLATIONS CONFIGURE_DEPENDS ${REPO_ROOT}/lib/I18n/translations/*.yaml)
add_custom_command(
  OUTPUT ${I18N_TEST_DIR}/I18nKeys.h ${I18N_TEST_DIR}/I18nStrings.h ${I18N_TEST_DIR}/I18nStrings.cpp
  COMMAND ${Python3_EXECUTABLE} -X utf8 ${REPO_ROOT}/scripts/gen_i18n.py
          ${REPO_ROOT}/lib/I18n/translations ${I18N_TEST_DIR}
  DEPENDS ${REPO_ROOT}/scripts/gen_i18n.py ${I18N_TRANSLATIONS}
  VERBATIM
)
add_executable(LanguageSettingsTest
  LanguageSettingsTest.cpp
  ${I18N_TEST_DIR}/I18n.cpp
  ${I18N_TEST_DIR}/I18nStrings.cpp
)
target_include_directories(LanguageSettingsTest PRIVATE
  ${I18N_TEST_DIR}
  ${REPO_ROOT}/test/reader_regressions/fakes
)
target_link_libraries(LanguageSettingsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LanguageSettingsTest)
add_test(NAME builtin_language_glyphs
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/BuiltinLanguageGlyphsTest.py)
