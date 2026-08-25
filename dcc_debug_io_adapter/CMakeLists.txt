cmake_minimum_required(VERSION 3.16)
project(dcc_debug_io_adapter C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL QUIET)
find_package(Threads REQUIRED)

set(DEFAULT_ENV_FILE ${CMAKE_CURRENT_BINARY_DIR}/altair_env.txt)
configure_file(assets/altair_env.example.txt ${DEFAULT_ENV_FILE} COPYONLY)

set(ADAPTER_SOURCES
    src/dcc_debug_io_adapter.c
    src/dcc_debug_terminal_input.c
    host/PortDrivers/io_ports.c
    host/PortDrivers/host_chat_io.c
    host/PortDrivers/environment_io.c
    host/PortDrivers/host_files_io.c
    host/PortDrivers/time_io.c
    host/PortDrivers/utility_io.c
    host/PortDrivers/weather_io.c
    port_drivers/json_scan.c
    port_drivers/interrupt_timer.c
)

add_library(dcc-debug-io-adapter SHARED ${ADAPTER_SOURCES})
target_include_directories(dcc-debug-io-adapter PRIVATE
    include
    host
    host/PortDrivers
    port_drivers
)
target_compile_definitions(dcc-debug-io-adapter PRIVATE
    DCC_DEBUG_IO_ADAPTER_BUILD=1
)
target_link_libraries(dcc-debug-io-adapter PRIVATE Threads::Threads)
set_target_properties(dcc-debug-io-adapter PROPERTIES
    C_VISIBILITY_PRESET hidden
    OUTPUT_NAME dcc-debug-io-adapter
)

if(CURL_FOUND AND TARGET CURL::libcurl)
    target_compile_definitions(dcc-debug-io-adapter PRIVATE HAVE_LIBCURL=1)
    target_link_libraries(dcc-debug-io-adapter PRIVATE CURL::libcurl)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(dcc-debug-io-adapter PRIVATE
        -Wall
        -Wextra
        -Wno-missing-field-initializers
    )
elseif(MSVC)
    target_compile_options(dcc-debug-io-adapter PRIVATE /W4)
    target_compile_definitions(dcc-debug-io-adapter PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(io-adapter-test tests/io_adapter_test.c)
    target_include_directories(io-adapter-test PRIVATE include)
    target_link_libraries(io-adapter-test PRIVATE dcc-debug-io-adapter)
    add_test(NAME io-adapter-lifecycle COMMAND io-adapter-test
             ${DEFAULT_ENV_FILE} ${CMAKE_CURRENT_BINARY_DIR})
endif()