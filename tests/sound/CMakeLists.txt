if(BUILD_TESTING)
    add_executable(scc1_tests scc1_test.cpp ../../src/sound/snd_mpu401.c)
    target_include_directories(scc1_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src/include
        ${PROJECT_SOURCE_DIR}/src/cpu
        ${PROJECT_BINARY_DIR}/src/include)
    target_compile_features(scc1_tests PRIVATE cxx_std_23)
    target_link_libraries(scc1_tests PRIVATE nuked_sc55 GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(scc1_tests)
endif()
