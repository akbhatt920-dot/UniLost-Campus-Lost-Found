cmake_minimum_required(VERSION 3.20)
project(UniLost LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT WIN32 AND NOT APPLE)
    message(FATAL_ERROR "This build currently supports native Windows and macOS.")
endif()

if(APPLE)
    find_program(BREW_EXECUTABLE brew)
    if(BREW_EXECUTABLE)
        execute_process(
            COMMAND ${BREW_EXECUTABLE} --prefix libpq
            OUTPUT_VARIABLE HOMEBREW_LIBPQ_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(HOMEBREW_LIBPQ_PREFIX)
            list(PREPEND CMAKE_PREFIX_PATH "${HOMEBREW_LIBPQ_PREFIX}")
            set(PostgreSQL_ROOT "${HOMEBREW_LIBPQ_PREFIX}")
        endif()
    endif()
endif()

find_package(PostgreSQL REQUIRED)

set(UNILOST_BACKEND_SOURCES
    backend/server.c
    backend/database.c
    backend/auth.c
    backend/matcher.c
    backend/mongoose.c
)

add_executable(unilost_server ${UNILOST_BACKEND_SOURCES})
target_include_directories(unilost_server PRIVATE backend)
target_compile_definitions(unilost_server PRIVATE MG_MAX_RECV_SIZE=65536)
target_link_libraries(unilost_server PRIVATE PostgreSQL::PostgreSQL)

if(WIN32)
    target_compile_definitions(unilost_server PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_libraries(unilost_server PRIVATE winhttp ws2_32)
elseif(APPLE)
    find_library(CFNETWORK_FRAMEWORK CFNetwork REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(unilost_server PRIVATE
        ${CFNETWORK_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
    )
endif()

if(MSVC)
    target_compile_options(unilost_server PRIVATE /W4)
else()
    target_compile_options(unilost_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(matcher_test tests/matcher_test.c backend/matcher.c)
target_include_directories(matcher_test PRIVATE backend)
if(MSVC)
    target_compile_options(matcher_test PRIVATE /W4)
else()
    target_compile_options(matcher_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME matcher_test COMMAND matcher_test)

add_executable(auth_key_test
    tests/auth_key_test.c
    backend/auth.c
    backend/mongoose.c
)
target_include_directories(auth_key_test PRIVATE backend)
target_compile_definitions(auth_key_test PRIVATE MG_MAX_RECV_SIZE=65536)
if(WIN32)
    target_compile_definitions(auth_key_test PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_link_libraries(auth_key_test PRIVATE winhttp ws2_32)
elseif(APPLE)
    target_link_libraries(auth_key_test PRIVATE
        ${CFNETWORK_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
    )
endif()
if(MSVC)
    target_compile_options(auth_key_test PRIVATE /W4)
else()
    target_compile_options(auth_key_test PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME auth_key_test COMMAND auth_key_test)
