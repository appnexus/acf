cmake_minimum_required(VERSION 2.8.0)
project(acf C CXX ASM)

option(DISABLE_TESTS "If tests should be compiled or not" OFF)

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "default to debug build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

option(SANITIZE "Enable Clang sanitizers" OFF)
if(SANITIZE AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(WARNING "SANITIZE is only supported for Clang ... disabling")
    set(SANITIZE OFF)
endif()

if(SANITIZE)
	message(STATUS "Enabling Clang sanitizers")
	set(ASAN_BUILD_FLAGS " -fno-sanitize-recover -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize=address ")
	SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ASAN_BUILD_FLAGS}")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${ASAN_BUILD_FLAGS}")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address -fsanitize=undefined ")
endif()

set(COMMON_FLAGS "${COMMON_FLAGS} -DMARCH_x86_64")
set(COMMON_FLAGS "${COMMON_FLAGS} -D_GNU_SOURCE")
set(COMMON_FLAGS "${COMMON_FLAGS} -Wall")
set(COMMON_FLAGS "${COMMON_FLAGS} -Werror")
set(COMMON_FLAGS "${COMMON_FLAGS} -Wextra")
set(COMMON_FLAGS "${COMMON_FLAGS} -Wformat=2")
set(COMMON_FLAGS "${COMMON_FLAGS} -Wno-unused-parameter")
set(COMMON_FLAGS "${COMMON_FLAGS} -Wno-unused-function")
set(COMMON_FLAGS "${COMMON_FLAGS} -Wno-format-nonliteral")
set(COMMON_FLAGS "${COMMON_FLAGS} -fms-extensions")
set(COMMON_FLAGS "${COMMON_FLAGS} -fno-omit-frame-pointer")
set(COMMON_FLAGS "${COMMON_FLAGS} -fno-strict-aliasing")
set(COMMON_FLAGS "${COMMON_FLAGS} -fvisibility=hidden")
set(COMMON_FLAGS "${COMMON_FLAGS} -fPIC")
set(COMMON_FLAGS "${COMMON_FLAGS} -g3")
set(COMMON_FLAGS "${COMMON_FLAGS} -pipe")
set(COMMON_FLAGS "${COMMON_FLAGS} -gdwarf-2")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-g2 -O2")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-g2 -O2")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/")
set(CMAKE_MACOSX_RPATH 1)

set(SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)

find_package(Threads REQUIRED)

include_directories(${SOURCE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_BINARY_DIR})

list(APPEND SRC
	${SOURCE_DIR}/an_allocator.c
	${SOURCE_DIR}/an_array.c
	${SOURCE_DIR}/an_charset.c
	${SOURCE_DIR}/an_itoa.c
	${SOURCE_DIR}/an_syslog.c
	${SOURCE_DIR}/an_util.c
)

set(LIBS
)

include(GenerateExportHeader)
add_library(acf SHARED ${SRC})
generate_export_header(acf)
target_link_libraries(acf ${LIBS})

set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)
set(VERSION_PATCH 1)
set(VERSION_STRING ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

set_target_properties(acf PROPERTIES VERSION ${VERSION_STRING} SOVERSION ${VERSION_MAJOR})

get_property(LIB64 GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)
if ("${LIB64}" STREQUAL "TRUE")
	set(LIBSUFFIX 64)
else()
	set(LIBSUFFIX "")
endif()

set(INSTALL_LIB_DIR lib${LIBSUFFIX} CACHE PATH "Installation directory for libraries")
mark_as_advanced(INSTALL_LIB_DIR)

install(TARGETS acf EXPORT acfTargets
    LIBRARY DESTINATION ${INSTALL_LIB_DIR}
    ARCHIVE DESTINATION ${INSTALL_LIB_DIR}
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

file(GLOB INSTALL_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h)
list(APPEND INSTALL_HEADERS ${PROJECT_BINARY_DIR}/acf_export.h)

install(FILES ${INSTALL_HEADERS} DESTINATION include/acf COMPONENT headers)

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/acf.pc.in
	       ${PROJECT_BINARY_DIR}/acf.pc)

install(FILES ${PROJECT_BINARY_DIR}/acf.pc
	DESTINATION "${CMAKE_INSTALL_PREFIX}/${INSTALL_LIB_DIR}/pkgconfig")

if (NOT ${DISABLE_TESTS})
	enable_testing()
	find_package(codecov)
	add_coverage(acf)
	add_subdirectory(test)
	coverage_evaluate()
    add_custom_target(coverage
        make && make test && make gcov && make lcov
    )
endif()
