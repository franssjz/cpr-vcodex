# Exercise the actual firmware decoder, not the simulator's stb_image shim.
# Keep this pin in step with platformio.ini; apply all production patches to
# an isolated copy so tests neither alter .pio nor require a firmware build.
find_package(Git REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
FetchContent_Declare(jpegdec_source
  GIT_REPOSITORY https://github.com/bitbank2/JPEGDEC.git
  GIT_TAG 86282979224c8a32fd51e091ed5a35b0c699a52b
)
FetchContent_GetProperties(jpegdec_source)
if(NOT jpegdec_source_POPULATED)
  FetchContent_Populate(jpegdec_source)
endif()

# Fail explicitly if firmware changes its decoder pin without updating this test.
file(READ "${REPO_ROOT}/platformio.ini" platformio_config)
if(NOT platformio_config MATCHES "JPEGDEC.git#86282979224c8a32fd51e091ed5a35b0c699a52b")
  message(FATAL_ERROR "Update the JPEGDEC regression test pin to match platformio.ini")
endif()

set(patched_source "${CMAKE_CURRENT_BINARY_DIR}/patched")
file(COPY "${jpegdec_source_SOURCE_DIR}/src" DESTINATION "${patched_source}")
# Give git apply an explicit root. Inside the firmware repository it would
# otherwise silently skip src/jpeg.inl as outside this build subdirectory.
execute_process(COMMAND "${GIT_EXECUTABLE}" init --quiet
  WORKING_DIRECTORY "${patched_source}" RESULT_VARIABLE init_result)
if(NOT init_result EQUAL 0)
  message(FATAL_ERROR "Could not initialize isolated JPEGDEC patch directory")
endif()
file(GLOB jpegdec_patches CONFIGURE_DEPENDS "${REPO_ROOT}/scripts/jpegdec_patches/*.patch")
list(SORT jpegdec_patches)
foreach(patch IN LISTS jpegdec_patches)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --no-index "${patch}"
    WORKING_DIRECTORY "${patched_source}"
    RESULT_VARIABLE patch_result
    ERROR_VARIABLE patch_error
  )
  if(NOT patch_result EQUAL 0)
    message(FATAL_ERROR "JPEGDEC patch failed: ${patch}\n${patch_error}")
  endif()
endforeach()

set(fixtures "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_fixtures.py" "${fixtures}"
  RESULT_VARIABLE fixture_result
)
if(NOT fixture_result EQUAL 0)
  message(FATAL_ERROR "Could not generate JPEG fixtures")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/generate_fixtures.py" ${jpegdec_patches})

add_executable(jpeg_scan_test JpegScanTest.cpp "${patched_source}/src/JPEGDEC.cpp")
target_include_directories(jpeg_scan_test PRIVATE "${patched_source}/src")
target_compile_definitions(jpeg_scan_test PRIVATE __LINUX__)
foreach(fixture baseline progressive_interleaved progressive_separate progressive_gray progressive_cr_table)
  add_test(NAME jpegdec_${fixture} COMMAND jpeg_scan_test "${fixtures}/${fixture}.jpg")
endforeach()
