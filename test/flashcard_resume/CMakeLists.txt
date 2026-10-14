add_executable(FlashcardResumeTest FlashcardResumeTest.cpp
  ${REPO_ROOT}/src/activities/apps/FlashcardReviewSession.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${REPO_ROOT}/src/FlashcardsStore.cpp)
file(READ ${REPO_ROOT}/src/FlashcardsStore.cpp store_source)
string(FIND "${store_source}" "bool saveJsonDocumentToFile(" save_start)
string(FIND "${store_source}" "bool loadJsonDocumentFromFile(" save_end)
math(EXPR save_length "${save_end} - ${save_start}")
string(SUBSTRING "${store_source}" ${save_start} ${save_length} save_method)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/SaveFlashcardJson.inc "${save_method}")
target_include_directories(FlashcardResumeTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes ${CMAKE_CURRENT_BINARY_DIR} ${REPO_ROOT}/src ${REPO_ROOT}/src/activities/apps)
target_link_libraries(FlashcardResumeTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(FlashcardResumeTest)
