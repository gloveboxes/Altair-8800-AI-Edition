/**
 * @file files_io.h
 * @brief Remote file transfer I/O port driver for Altair 8800 emulator on ESP32
 *
 * Implements the FT protocol over TCP to the Remote FT server.
 * Shared-service group 0x70 adapts the private driver selectors 60/61.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

// Chunk size for file transfers
#define FT_CHUNK_SIZE 256
#define FT_MAX_FILENAME 128

// Private command selector 60
typedef enum
{
    FT_CMD_NOP = 0,           // No operation
    FT_CMD_SET_FILENAME = 1,  // Begin filename transfer
    FT_CMD_REQUEST_CHUNK = 3, // Request next 256-byte chunk
    FT_CMD_CLOSE = 4          // Close file transfer
} ft_command_t;

// Private status selector 60
typedef enum
{
    FT_STATUS_IDLE = 0,      // Idle, ready for new transfer
    FT_STATUS_DATAREADY = 1, // Data ready to read
    FT_STATUS_EOF = 2,       // End of file reached
    FT_STATUS_BUSY = 3,      // Transfer in progress, not ready for new command
    FT_STATUS_ERROR = 0xFF   // Error occurred
} ft_status_t;

// Initialize file transfer port driver
void files_io_init(void);

// Private driver selectors, called only from the CPU thread.
size_t files_output(int port, uint8_t data, char* buffer, size_t buffer_length);

// Handle input from file transfer ports (called from emulator core)
uint8_t files_input(uint8_t port);
