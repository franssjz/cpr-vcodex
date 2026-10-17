set(activity_source ${REPO_ROOT}/src/activities/browser/OpdsBookBrowserActivity.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${activity_source})
file(READ ${activity_source} opds_source)
function(extract_opds_methods first last output)
  string(FIND "${opds_source}" "${first}" start)
  string(FIND "${opds_source}" "${last}" end)
  if(start LESS 0 OR end LESS_EQUAL start)
    message(FATAL_ERROR "Cannot locate OPDS production methods: ${first}")
  endif()
  math(EXPR length "${end} - ${start}")
  string(SUBSTRING "${opds_source}" ${start} ${length} methods)
  set(${output} "${methods}" PARENT_SCOPE)
endfunction()
extract_opds_methods("void OpdsBookBrowserActivity::fetchFeed(" "void OpdsBookBrowserActivity::navigateToEntry("
  FEED_METHODS)
extract_opds_methods("void OpdsBookBrowserActivity::startBrowse()" "void OpdsBookBrowserActivity::downloadBook("
  BROWSE_METHODS)
configure_file(FeedLoadingAdapter.h.in FeedLoadingAdapter.h @ONLY)

add_executable(OpdsFeedLoadingTest OpdsFeedLoadingTest.cpp InstrumentedParser.cpp ${REPO_ROOT}/src/util/UrlUtils.cpp)
target_include_directories(OpdsFeedLoadingTest PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/OpdsParser
  ${REPO_ROOT}/lib/XmlParserUtils
)
target_link_libraries(OpdsFeedLoadingTest PRIVATE crosspoint_test_common GTest::gtest_main EXPAT::EXPAT)
gtest_discover_tests(OpdsFeedLoadingTest)

set(downloader_source ${REPO_ROOT}/src/network/HttpDownloader.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${downloader_source})
file(READ ${downloader_source} http_source)
string(FIND "${http_source}" "HttpDownloader::DownloadError HttpDownloader::downloadToFile(" start)
if(start LESS 0)
  message(FATAL_ERROR "Cannot locate production downloadToFile method")
endif()
string(SUBSTRING "${http_source}" ${start} -1 DOWNLOAD_METHOD)
string(FIND "${http_source}" "struct Sink {" start)
if(start LESS 0)
  message(FATAL_ERROR "Cannot locate production download Sink")
endif()
string(SUBSTRING "${http_source}" ${start} -1 sink_source)
string(FIND "${sink_source}" "};" end)
math(EXPR length "${end} + 2")
string(SUBSTRING "${sink_source}" 0 ${length} SINK_DEFINITION)
configure_file(DownloadFileAdapter.h.in DownloadFileAdapter.h @ONLY)

add_executable(HttpDownloadFileTest HttpDownloadFileTest.cpp)
target_include_directories(HttpDownloadFileTest PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
)
target_link_libraries(HttpDownloadFileTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(HttpDownloadFileTest)
