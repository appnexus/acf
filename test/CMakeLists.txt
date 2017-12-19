include(CheckTest)

find_package(Check REQUIRED)

set(CHECK_LIBS ${CMAKE_THREAD_LIBS_INIT})

CHECK_TEST(check_an_array LIBS ${CHECK_LIBS} SRC ${SRC} check_an_array.c)
CHECK_TEST(check_charset LIBS ${CHECK_LIBS} SRC ${SRC} check_charset.c)
CHECK_TEST(check_bitmap LIBS ${CHECK_LIBS} SRC ${SRC} check_bitmap.c)
CHECK_TEST(check_an_array_cpp LIBS ${CHECK_LIBS} SRC ${SRC} check_an_array_cpp.cc)