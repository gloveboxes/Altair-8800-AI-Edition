#ifdef NDEBUG
#undef NDEBUG
#endif
#include "io_ports.h"
#include "io_service.h"
#include "files_io.h"
#include "host_files_io.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

bool io_service_init(void) { return true; }
void io_service_shutdown(void) {}
const io_service_result_t *io_service_poll(void) { return NULL; }
io_service_error_t io_service_submit(uint8_t command, const uint8_t *data,
                                    size_t length, uint32_t generation) {
    (void)command; (void)data; (void)length; (void)generation;
    return IO_SERVICE_UNAVAILABLE;
}
uint64_t time_elapsed_ms(void) { return 0; }
void interrupt_timer_output(uint8_t data) { (void)data; }
uint8_t interrupt_timer_input(void) { return 0; }

static void send_request(const uint8_t *data, size_t length) {
    assert(io_port_in(2) == IO_SERVICE_READY);
    io_port_out(2, IO_SERVICE_HTTP);
    io_port_out(3, (uint8_t)length);
    io_port_out(3, (uint8_t)(length >> 8));
    for (size_t i = 0; i < length; ++i) io_port_out(3, data[i]);
}
static size_t response(uint8_t *data) {
    size_t n = io_port_in(4);
    n |= (size_t)io_port_in(4) << 8;
    assert(n <= 257);
    for (size_t i = 0; i < n; ++i) data[i] = io_port_in(4);
    return n;
}
static void error(const uint8_t *data, size_t n, uint8_t expected) {
    uint8_t reply[257];
    send_request(data, n);
    assert(io_port_in(2) == IO_SERVICE_ERROR);
    assert(response(reply) == 1 && reply[0] == expected);
    io_port_out(2, IO_SERVICE_ACK);
    assert(io_port_in(2) == IO_SERVICE_READY);
}
static void open_file(const char *name) {
    uint8_t request[FT_MAX_FILENAME] = {FT_CMD_SET_FILENAME};
    size_t n = strlen(name);
    assert(n < sizeof(request));
    memcpy(request + 1, name, n);
    send_request(request, n + 1);
    assert(io_port_in(2) == IO_SERVICE_COMPLETE);
    io_port_out(2, IO_SERVICE_ACK);
}
static void close_file(void) {
    send_request((const uint8_t[]){FT_CMD_CLOSE}, 1);
    assert(io_port_in(2) == IO_SERVICE_COMPLETE);
    io_port_out(2, IO_SERVICE_ACK);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    host_files_init(argv[1]);
    assert(io_ports_init());
    const uint8_t read[] = {FT_CMD_REQUEST_CHUNK};
    error(read, 1, IO_SERVICE_HTTP_TRANSFER);
    error((const uint8_t[]){0}, 1, IO_SERVICE_UNKNOWN_OPERATION);
    error((const uint8_t[]){1}, 1, IO_SERVICE_INVALID_LENGTH);
    error((const uint8_t[]){1, 'a', 0}, 3, IO_SERVICE_INVALID_PARAMETER);
    error((const uint8_t[]){3, 0}, 2, IO_SERVICE_INVALID_LENGTH);
    error((const uint8_t[]){4, 0}, 2, IO_SERVICE_INVALID_LENGTH);
    uint8_t oversized[FT_MAX_FILENAME + 1];
    memset(oversized, 'x', sizeof(oversized));
    oversized[0] = 1;
    error(oversized, sizeof(oversized), IO_SERVICE_INVALID_LENGTH);

    char path[1024];
    assert(snprintf(path, sizeof(path), "%s/FTTEST.BIN", argv[1]) > 0);
    const unsigned sizes[] = {0, 1, 127, 128, 255, 256, 257, 513, 70000};
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        FILE *file = fopen(path, "wb");
        assert(file);
        for (unsigned i = 0; i < sizes[s]; ++i) assert(fputc((uint8_t)i, file) != EOF);
        assert(fclose(file) == 0);
        open_file("fttest.bin");
        unsigned position = 0;
        uint8_t reply[257];
        do {
            send_request(read, 1);
            assert(io_port_in(2) == IO_SERVICE_RESPONSE_READY);
            size_t n = response(reply);
            assert(n >= 1 && n <= 257 && (reply[0] == 0 || reply[0] == 2));
            for (size_t i = 1; i < n; ++i) assert(reply[i] == (uint8_t)position++);
            assert(io_port_in(2) == IO_SERVICE_READY);
            io_port_out(2, IO_SERVICE_IDENTIFY);
            assert(response(oversized) == 13 && !memcmp(oversized, "ROMLESS-SVC/1", 13));
            for (unsigned port = 0; port < 256; ++port) {
                if ((port & 0x17) != 0x15 && (port & 0x17) != 0x16) continue;
                io_port_out((uint8_t)port, FT_CMD_CLOSE);
                assert(io_port_in((uint8_t)port) == 0);
            }
        } while (reply[0] != FT_STATUS_EOF);
        assert(position == sizes[s]);
        send_request(read, 1);
        assert(io_port_in(2) == IO_SERVICE_RESPONSE_READY);
        assert(response(reply) == 1 && reply[0] == FT_STATUS_EOF);
        close_file();
    }

    /* Reopen after discarding half a framed chunk; no stale bytes survive. */
    open_file("FTTEST.BIN");
    send_request(read, 1);
    assert(io_port_in(2) == IO_SERVICE_RESPONSE_READY);
    assert(io_port_in(4) == 1 && io_port_in(4) == 1);
    assert(io_port_in(4) == 0 && io_port_in(4) == 0);
    io_port_out(2, IO_SERVICE_RESET);
    open_file("FTTEST.BIN");
    send_request(read, 1);
    uint8_t reply[257];
    assert(io_port_in(2) == IO_SERVICE_RESPONSE_READY && response(reply) == 257);
    assert(reply[1] == 0 && reply[256] == 255);
    close_file();
    open_file("MISSING.BIN");
    error(read, 1, IO_SERVICE_HTTP_TRANSFER);
    close_file();
    open_file("FTTEST.BIN");
    close_file();
    error(read, 1, IO_SERVICE_HTTP_TRANSFER);
    assert(remove(path) == 0);
    io_ports_shutdown();
    puts("PASS: file shared framing, boundaries, 70K binary stream, coexistence, recovery and freed ports");
    return 0;
}
