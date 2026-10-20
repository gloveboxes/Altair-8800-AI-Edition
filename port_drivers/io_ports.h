#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IO_SERVICE_COMMAND_PORT  0x02
#define IO_SERVICE_REQUEST_PORT  0x03
#define IO_SERVICE_RESPONSE_PORT 0x04
#define IO_INTERRUPT_TIMER_PORT  0x07
#define IO_FILE_COMMAND_PORT     0x15
#define IO_FILE_DATA_PORT        0x16
#define IO_TIMER_PORT            0x17
#define IO_TIMER_RESET           0x00
#define IO_TIMER_START           0x10
#define IO_TIMER_QUERY           0x20
#define IO_TIMER_MASK            0x30
#define IO_STOPWATCH_RESET       0x40
#define IO_STOPWATCH_SECONDS     0x50
#define IO_STOPWATCH_MS          0x60
#define IO_TIMER_ERROR           0xff
#define IO_SERVICE_MAX_PAYLOAD   2048

typedef enum
{
    IO_SERVICE_ACK = 0x00,
    IO_SERVICE_RESET = 0x01,
    IO_SERVICE_IDENTIFY = 0x02,
    IO_SERVICE_TIMERS = 0x10,
    IO_SERVICE_STOPWATCHES = 0x11,
    IO_SERVICE_CLOCK = 0x20,
    IO_SERVICE_UTILITY = 0x21,
    IO_SERVICE_ENVIRONMENT = 0x30,
    IO_SERVICE_WEATHER = 0x40,
    IO_SERVICE_CHAT = 0x50,
    IO_SERVICE_SENSE_HAT = 0x60
} io_service_command_t;

typedef enum
{
    IO_SERVICE_READY = 0x00,
    IO_SERVICE_COMPLETE = 0x01,
    IO_SERVICE_RESPONSE_READY = 0x02,
    IO_SERVICE_COLLECTING = 0x03,
    IO_SERVICE_BUSY = 0x04,
    IO_SERVICE_ERROR = 0xff
} io_service_status_t;

typedef enum
{
    IO_SERVICE_OK = 0,
    IO_SERVICE_UNKNOWN_GROUP = 0x01,
    IO_SERVICE_UNKNOWN_OPERATION = 0x02,
    IO_SERVICE_INVALID_LENGTH = 0x03,
    IO_SERVICE_INVALID_PARAMETER = 0x04,
    IO_SERVICE_UNAVAILABLE = 0x05,
    IO_SERVICE_BUSY_ERROR = 0x06,
    IO_SERVICE_TIMEOUT = 0x07,
    IO_SERVICE_INVALID_STATE = 0x08,
    IO_SERVICE_UNDERFLOW = 0x09,
    IO_SERVICE_STORAGE_OPEN = 0x10,
    IO_SERVICE_STORAGE_READ = 0x11,
    IO_SERVICE_STORAGE_WRITE = 0x12,
    IO_SERVICE_CAPACITY = 0x13,
    IO_SERVICE_NOT_FOUND = 0x14,
    IO_SERVICE_CLOCK_UNSYNCED = 0x15,
    IO_SERVICE_REBOOT_CONFIRMATION = 0x16
} io_service_error_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Call after driver initialization, before starting the CPU. */
bool io_ports_init(void);
void io_ports_shutdown(void);
uint8_t io_port_in(uint8_t port);
void io_port_out(uint8_t port, uint8_t data);

#ifdef __cplusplus
}
#endif
