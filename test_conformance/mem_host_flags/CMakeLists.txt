add_executable(test_mem_host_flags
    main.cpp
    mem_host_buffer.cpp
    mem_host_image.cpp
    ../../test_common/harness/errorHelpers.c
    ../../test_common/harness/threadTesting.c
    ../../test_common/harness/testHarness.c
    ../../test_common/harness/genericThread.cpp
    ../../test_common/harness/kernelHelpers.c
    ../../test_common/harness/typeWrappers.cpp
    ../../test_common/harness/mt19937.c
    ../../test_common/harness/conversions.c
    ../../test_common/harness/ThreadPool.c
)

add_definitions(-DDONT_TEST_GARBAGE_POINTERS)

set_source_files_properties(
    main.cpp
    mem_host_buffer.cpp
    mem_host_image.cpp
    ../../test_common/harness/errorHelpers.c
    ../../test_common/harness/threadTesting.c
    ../../test_common/harness/testHarness.c
    ../../test_common/harness/genericThread.cpp
    ../../test_common/harness/kernelHelpers.c
    ../../test_common/harness/typeWrappers.cpp
    ../../test_common/harness/mt19937.c
    ../../test_common/harness/conversions.c
    ../../test_common/harness/ThreadPool.c
    PROPERTIES LANGUAGE CXX)

TARGET_LINK_LIBRARIES(test_mem_host_flags
        ${CLConform_LIBRARIES})
