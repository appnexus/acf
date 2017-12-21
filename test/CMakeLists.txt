include(CheckTest)

find_package(Check REQUIRED)

set(CHECK_LIBS acf ${CMAKE_THREAD_LIBS_INIT})

CHECK_TEST(check_an_allocator LIBS ${CHECK_LIBS} SRC check_an_allocator.c)
CHECK_TEST(check_an_array LIBS ${CHECK_LIBS} SRC check_an_array.c)
CHECK_TEST(check_an_array_cpp LIBS ${CHECK_LIBS} SRC check_an_array_cpp.cc)
CHECK_TEST(check_an_charset LIBS ${CHECK_LIBS} SRC check_an_charset.c)
CHECK_TEST(check_an_itoa LIBS ${CHECK_LIBS} SRC check_an_itoa.c)
CHECK_TEST(check_an_util LIBS ${CHECK_LIBS} SRC check_an_util.c)
CHECK_TEST(check_bitmap LIBS ${CHECK_LIBS} SRC check_bitmap.c)
