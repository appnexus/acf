if (NOT ${DISABLE_TESTS})
    include_directories(${CHECK_INCLUDE_DIR})
    # Build output directory for our check results. It is annoying that we have to
    # do this.
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/check")
endif()

macro(CHECK_TEST test_name)
    set(options OPTIONAL)
    set(oneValueArgs NAME)
    set(multiValueArgs LIBS SRC DEPS)
    cmake_parse_arguments(CHECK_TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(CHECK_TEST_NAME2 ${test_name})

    if (NOT ${DISABLE_TESTS})
        add_executable(${CHECK_TEST_NAME2} ${CHECK_TEST_SRC})
        target_link_libraries(${CHECK_TEST_NAME2} ${CHECK_TEST_LIBS} ${CHECK_LIBRARIES})
        add_test(NAME ${CHECK_TEST_NAME2} COMMAND $<TARGET_FILE:${CHECK_TEST_NAME2}> WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
        if (DEFINED CHECK_TEST_DEPS)
            add_dependencies(${CHECK_TEST_NAME2} ${CHECK_TEST_DEPS})
        endif()
    endif()
endmacro()
