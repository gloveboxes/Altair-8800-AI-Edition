/**
 * @file io_ports.c
 * @brief I/O port handler for the Altair 8800 host emulator and MCP server.
 *
 * Routes I/O port operations to the appropriate drivers based on port number.
 *
 * MIRROR FILE: the switch bodies below are kept structurally identical to the
 * ESP32 firmware's port_drivers/io_ports.c, which is the SOURCE OF TRUTH for
 * the port -> driver mapping. Only the #include block differs (host driver
 * headers vs the firmware's port_drivers/ headers); the files driver is reached
 * through the host files_output()/files_input() shim. When the mapping changes,
 * update the ESP32 file first and re-sync this one.
 *
 * One host-only addition: the Raspberry Pi Sense HAT front-panel ports
 * (63, 65, 80, 81, 85, 90-102) are dispatched to sense_hat_panel_output().
 * That peripheral is Linux/Docker-only and is not present in the ESP32
 * firmware, so this case group has no counterpart in the source-of-truth file.
 */

#include "io_ports.h"

#include "PortDrivers/chat_io.h"
#include "PortDrivers/environment_io.h"
#include "PortDrivers/host_files_io.h"
#include "PortDrivers/time_io.h"
#include "PortDrivers/utility_io.h"
#include "PortDrivers/weather_io.h"
#include "drivers/sense_hat/sense_hat_panel.h"

#include <stdio.h>
#include <string.h>

#define REQUEST_BUFFER_SIZE 2048

typedef struct
{
    size_t len;
    size_t count;
    char buffer[REQUEST_BUFFER_SIZE];
} request_unit_t;

static request_unit_t request_unit;

void io_port_out(uint8_t port, uint8_t data)
{
    request_unit.len = 0;
    request_unit.count = 0;
    request_unit.buffer[0] = '\0';

    switch (port)
    {
        // Time/timer ports
        case 24:
        case 25:
        case 26:
        case 27:
        case 28:
        case 29:
        case 30:
        case 31:
        case 37:
        case 38:
        case 39:
        case 41:
        case 42:
        case 43:
        case 44:
            request_unit.len = time_output(port, data, request_unit.buffer, sizeof(request_unit.buffer));
            break;

        // Utility ports
        case 45:
        case 48:
        case 49:
        case 70:
            request_unit.len = utility_output(port, data, request_unit.buffer, sizeof(request_unit.buffer));
            break;

        // Weather field port (OpenWeatherMap)
        case 46:
            request_unit.len = weather_output(port, data, request_unit.buffer, sizeof(request_unit.buffer));
            break;

        // Chat ports (OpenAI / compatible)
        case 120:
        case 121:
        case 122:
            chat_output(port, data, request_unit.buffer, sizeof(request_unit.buffer));
            break;

        // Files ports (60, 61)
        case 60:
        case 61:
            files_output(port, data, request_unit.buffer, sizeof(request_unit.buffer));
            break;

        // Environment variable ports (NVS-backed)
        case ENVIRONMENT_PORT_COMMAND:
        case ENVIRONMENT_PORT_DATA:
            request_unit.len = environment_output(port, data, request_unit.buffer, sizeof(request_unit.buffer));
            break;

        // Raspberry Pi Sense HAT front panel + sensors (host-only addition, not
        // in the ESP32 firmware mirror). Inert when the panel is not active.
        case 63:  // onboard sensors (temperature/pressure/light/humidity)
        case 65:  // LED panel color
        case 80:  // panel mode (bus/font/bitmap)
        case 81:  // font color
        case 85:  // display character
        case 90:  // bitmap rows 0-7
        case 91:
        case 92:
        case 93:
        case 94:
        case 95:
        case 96:
        case 97:
        case 98:  // pixel on
        case 99:  // pixel off
        case 100: // pixel flip
        case 101: // clear all pixels
        case 102: // bitmap draw
            request_unit.len = sense_hat_panel_output(port, data, request_unit.buffer, sizeof(request_unit.buffer));
            break;

        default:
            break;
    }
}

uint8_t io_port_in(uint8_t port)
{
    switch (port)
    {
        // Time/timer ports
        case 24:
        case 25:
        case 26:
        case 27:
        case 28:
        case 29:
        case 30:
            return time_input(port);

        // Weather status
        case 47:
            return weather_input(port);

        // Request buffer read port
        case 200:
            if (request_unit.count < request_unit.len && request_unit.count < sizeof(request_unit.buffer))
            {
                return (uint8_t)request_unit.buffer[request_unit.count++];
            }
            return 0x00;

        // Chat ports (OpenAI / compatible)
        case 120:
        case 123:
        case 124:
            return chat_input(port);

        // Files ports (60, 61)
        case 60:
        case 61:
            return files_input(port);

        // Environment status port
        case ENVIRONMENT_PORT_COMMAND:
            return environment_input(port);

        default:
            return 0x00;
    }
}
