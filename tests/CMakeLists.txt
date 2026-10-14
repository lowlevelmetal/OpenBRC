# One GoogleTest executable per module: tests/<module>/test_*.cpp -> test_<module>.
# Tests that need the original game read $OPENBRC_GAME_DATA and skip without it.
add_library(brc_testdata STATIC TestData.cpp)
target_link_libraries(brc_testdata PUBLIC OpenBRC::vfs GTest::gtest)
target_include_directories(brc_testdata PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

include(GoogleTest)
foreach(module IN LISTS OPENBRC_MODULES)
    file(GLOB test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${module}/test_*.cpp")
    if(test_sources AND TARGET brc_${module})
        add_executable(test_${module} ${test_sources})
        target_link_libraries(test_${module} PRIVATE brc_${module} brc_testdata GTest::gtest_main)
        gtest_discover_tests(test_${module} DISCOVERY_MODE PRE_TEST)
    endif()
endforeach()
