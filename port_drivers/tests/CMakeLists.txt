add_executable(io-service-test
    io_service_test.c
    ../io_ports.c
    ../io_service.c
)
target_include_directories(io-service-test PRIVATE .. ../../altair_local)
target_compile_definitions(io-service-test PRIVATE ALTAIR_LOCAL_SERVICE_SENSE_HAT=1)
target_link_libraries(io-service-test PRIVATE Threads::Threads)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(io-service-test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME io-service-protocol COMMAND io-service-test)

add_executable(io-ports-hot-path-test io_ports_hot_path_test.c ../io_ports.c)
target_include_directories(io-ports-hot-path-test PRIVATE ..)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(io-ports-hot-path-test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME io-ports-hot-path COMMAND io-ports-hot-path-test)

# Keep the dcc client's uppercase filename out of C++ detection.
configure_file(../../Apps/COMMON/DXSVC.C ${CMAKE_CURRENT_BINARY_DIR}/dxsvc_client.c COPYONLY)
configure_file(../../Apps/COMMON/DXSVC.H ${CMAKE_CURRENT_BINARY_DIR}/DXSVC.H COPYONLY)
configure_file(../../Apps/COMMON/tests/SVCCORE.C ${CMAKE_CURRENT_BINARY_DIR}/svccore_ref.c COPYONLY)
add_executable(cpm-service-client-test
    ../../Apps/COMMON/tests/svc_test.c
    ${CMAKE_CURRENT_BINARY_DIR}/dxsvc_client.c
    ${CMAKE_CURRENT_BINARY_DIR}/svccore_ref.c
)
set_property(TARGET cpm-service-client-test PROPERTY C_STANDARD 99)
target_compile_definitions(cpm-service-client-test PRIVATE exit=svc_test_exit)
add_test(NAME cpm-service-client COMMAND cpm-service-client-test)

add_executable(io-timer-test
    io_timer_test.c
    ../io_ports.c
    ${CMAKE_CURRENT_BINARY_DIR}/dxsvc_client.c
    ${CMAKE_CURRENT_BINARY_DIR}/svccore_ref.c
)
target_include_directories(io-timer-test PRIVATE ..)
set_property(TARGET io-timer-test PROPERTY C_STANDARD 99)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(io-timer-test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME io-timer-port COMMAND io-timer-test)

add_executable(files-service-test files_service_test.c ../io_ports.c
    ../../altair_local/PortDrivers/host_files_io.c)
target_include_directories(files-service-test PRIVATE .. ../../altair_local/PortDrivers)
add_test(NAME files-shared-service COMMAND files-service-test ${CMAKE_CURRENT_BINARY_DIR})

add_executable(files-adapter-service-test files_service_test.c ../io_ports.c
    ../../dcc_debug_io_adapter/host/PortDrivers/host_files_io.c)
target_include_directories(files-adapter-service-test PRIVATE .. ../../dcc_debug_io_adapter/host/PortDrivers)
add_test(NAME files-adapter-shared-service COMMAND files-adapter-service-test ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(files-shared-service files-adapter-shared-service PROPERTIES RUN_SERIAL TRUE)

if(UNIX)
    add_executable(files-esp-test files_esp_test.c ../io_ports.c)
    target_include_directories(files-esp-test PRIVATE files_fakes .. ../..)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(files-esp-test PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME files-esp-queues COMMAND files-esp-test)
endif()
foreach(target files-service-test files-adapter-service-test)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endforeach()
