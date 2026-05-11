/**
 * @file environment_io.h
 * @brief NVS-backed environment variable I/O port driver.
 *
 * Port map:
 *   OUT 71, 0      reset request buffer
 *   OUT 71, cmd    execute ENV command/API request
 *   OUT 72, byte   append request byte
 *   IN  71         last ENV status code
 *   IN  200        response bytes, NUL-terminated by io_ports
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define ENVIRONMENT_PORT_COMMAND 71
#define ENVIRONMENT_PORT_DATA    72

void environment_io_init(void);
size_t environment_output(int port, uint8_t data, char *buffer, size_t buffer_length);
uint8_t environment_input(uint8_t port);

#ifdef __cplusplus
}
#endif
