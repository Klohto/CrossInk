add_executable(ReaderRasterTest ReaderRasterTest.cpp ${REPO_ROOT}/lib/Epub/Epub/ReaderRaster.cpp)
target_include_directories(ReaderRasterTest PRIVATE ../section_persistence/stubs ${REPO_ROOT}/lib/Epub)
target_link_libraries(ReaderRasterTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderRasterTest)
add_executable(HtmlEventCacheTest HtmlEventCacheTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/parsers/HtmlEventCache.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ReaderRaster.cpp)
target_include_directories(HtmlEventCacheTest PRIVATE ../section_persistence/stubs ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Serialization ${REPO_ROOT}/lib/Memory)
target_link_libraries(HtmlEventCacheTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(HtmlEventCacheTest)
add_executable(CarouselArrowGestureTest CarouselArrowGestureTest.cpp)
target_link_libraries(CarouselArrowGestureTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CarouselArrowGestureTest)

add_executable(ReaderWakeFrameTest ReaderWakeFrameTest.cpp
  ${REPO_ROOT}/src/util/ReaderWakeFrame.cpp ${REPO_ROOT}/lib/Epub/Epub/ReaderRaster.cpp)
target_include_directories(ReaderWakeFrameTest PRIVATE stubs ../section_persistence/stubs
  ${REPO_ROOT}/lib/Epub ${REPO_ROOT}/lib/Memory)
target_link_libraries(ReaderWakeFrameTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderWakeFrameTest)
