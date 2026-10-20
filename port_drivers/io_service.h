#pragma once

#include "io_ports.h"

#include <stddef.h>

typedef struct
{
    uint32_t generation;
    io_service_error_t error;
    bool has_response;
    size_t length;
    uint8_t data[IO_SERVICE_MAX_PAYLOAD + 1];
} io_service_result_t;

bool io_service_init(void);
void io_service_shutdown(void);
io_service_error_t io_service_submit(uint8_t command, const uint8_t *data,
                                    size_t length, uint32_t generation);
const io_service_result_t *io_service_poll(void);
