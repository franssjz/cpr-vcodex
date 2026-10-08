cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# Exercise the same parser configuration as the firmware on every host.
add_library(expat STATIC
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)
target_include_directories(expat PUBLIC ${REPO_ROOT}/lib/expat)
target_compile_definitions(expat PUBLIC XML_STATIC XML_GE=0 XML_CONTEXT_BYTES=1024)
add_library(EXPAT::EXPAT ALIAS expat)

add_library(crosspoint_test_common INTERFACE)
target_include_directories(crosspoint_test_common INTERFACE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/MemoryBudget
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
)
if(MSVC)
  target_compile_options(crosspoint_test_common INTERFACE /W4 /Zc:preprocessor /utf-8)
  target_include_directories(crosspoint_test_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/windows_compat)
else()
  target_compile_options(crosspoint_test_common INTERFACE
    -Wall
    -Wextra
    -pedantic
  )
endif()

enable_testing()
include(GoogleTest)

# Upstream suites
add_subdirectory(streaming_json_parser)
add_subdirectory(release_json_parser)
add_subdirectory(differential_rounding)
add_subdirectory(hyphenation_eval)
add_subdirectory(utf8_compose)
add_subdirectory(opds_filename)
add_subdirectory(html_to_plain_text)
add_subdirectory(library_text)
add_subdirectory(library_format)
add_subdirectory(library_index_file)
add_subdirectory(library_builder)
add_subdirectory(minibidi_arabic)
add_subdirectory(combining_marks)
add_subdirectory(credential_integrity)
add_subdirectory(xml_parser_utils)
add_subdirectory(token_boundary)
add_subdirectory(koreader_xpath_resolver)
add_subdirectory(progress_comparison)
add_subdirectory(font_cache_manager)
add_subdirectory(css_parser)
add_subdirectory(chapter_html_slim_parser)
add_subdirectory(korean_line_breaking)
add_subdirectory(ligature_guard)
add_subdirectory(page_link)

# Fork (CPR-vCodex) suites
add_subdirectory(reader_position)
add_subdirectory(reader_regressions)
add_subdirectory(language_settings)
add_subdirectory(ota_update)
add_subdirectory(jpegdec)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME builtin_kerning_regressions
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/differential_rounding/BuiltinKerningTest.py)
  add_test(NAME autoflash_manifest_regressions
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s test/release_tools -p AutoflashManifestTest.py)
  set_tests_properties(autoflash_manifest_regressions PROPERTIES WORKING_DIRECTORY ${REPO_ROOT})
  add_test(NAME firmware_packaging_regressions
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/release_tools/FirmwarePackagingTest.py)
  add_test(
    NAME web_optimizer_regressions
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_optimizer_regressions/EpubOptimizerFallbackTest.py
  )
  add_test(
    NAME web_flash_regressions
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/web_flash_regressions/FlashPageTest.py
  )
  add_test(
    NAME firmware_budget_report_regressions
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/release_tools/FirmwareBudgetReportTest.py
  )
endif()
add_subdirectory(content_opf_parser)
add_subdirectory(chapter_position)
add_subdirectory(home_button)
add_subdirectory(button_press_navigation)

add_subdirectory(inflate_stream)
add_subdirectory(fs_helpers)
add_subdirectory(txt_to_html)

add_subdirectory(absolute_grayscale)

add_subdirectory(sd_card_font)
add_subdirectory(glyph_bitmap)

add_subdirectory(reader_session)
add_subdirectory(reader_plugin_session)
add_subdirectory(header_status_layout)
add_subdirectory(popup_viewport)
add_subdirectory(list_navigation)
add_subdirectory(plugin_manifest_classification)
add_subdirectory(protected_paths)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME TrustedTimeTimezone COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_trusted_time.py)
