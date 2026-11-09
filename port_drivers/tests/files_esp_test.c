#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../files_io.c"
#include "io_service.h"

struct test_queue {
    size_t depth, size, count;
    uint8_t bytes[2 * sizeof(ft_response_meta_t)];
};

QueueHandle_t xQueueCreate(size_t depth, size_t size) {
    assert(depth * size <= sizeof(((struct test_queue *)0)->bytes));
    QueueHandle_t q = calloc(1, sizeof(*q));
    assert(q);
    q->depth = depth;
    q->size = size;
    return q;
}
void vQueueDelete(QueueHandle_t q) { free(q); }
BaseType_t xQueueSend(QueueHandle_t q, const void *item, uint32_t wait) {
    assert(wait == 0);
    if (q->count == q->depth) return 0;
    memcpy(q->bytes + q->count++ * q->size, item, q->size);
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *item, uint32_t wait) {
    assert(wait == 0);
    if (!q->count) return 0;
    memcpy(item, q->bytes, q->size);
    --q->count;
    memmove(q->bytes, q->bytes + q->size, q->count * q->size);
    return pdTRUE;
}
BaseType_t xQueueOverwrite(QueueHandle_t q, const void *item) {
    assert(q->depth == 1);
    q->count = 1;
    memcpy(q->bytes, item, q->size);
    return pdTRUE;
}
BaseType_t xTaskCreatePinnedToCore(void (*entry)(void *), const char *name,
                                  unsigned stack, void *arg, unsigned priority,
                                  void *handle, unsigned core) {
    assert(entry == ft_client_task && !strcmp(name, "ft_client"));
    assert(stack == FT_TASK_STACK_SIZE && !arg && !handle);
    assert(priority == FT_TASK_PRIORITY && core == 0);
    return pdPASS;
}

static bool connected = true, socket_ok = true, truncated;
static uint8_t wire[FT_CHUNK_SIZE + 2], sent[FT_MAX_FILENAME + 6];
static size_t wire_length, wire_position, sent_length;
static unsigned close_calls;
bool wifi_is_connected(void) { return connected; }
const char *config_get_rfs_ip(void) { return "192.0.2.1"; }
int test_socket(int domain, int type, int protocol) {
    assert(domain == AF_INET && type == SOCK_STREAM && protocol == IPPROTO_IP);
    return socket_ok ? 42 : -1;
}
int test_connect(int fd, const struct sockaddr *address, socklen_t length) {
    assert(fd == 42 && length == sizeof(struct sockaddr_in));
    assert(ntohs(((const struct sockaddr_in *)address)->sin_port) == FT_SERVER_PORT);
    return 0;
}
int test_setsockopt(int fd, int level, int option, const void *value, socklen_t length) {
    (void)level; (void)option; (void)value; (void)length;
    assert(fd == 42);
    return 0;
}
int test_close(int fd) { assert(fd == 42); ++close_calls; return 0; }
ssize_t test_send(int fd, const void *bytes, size_t length, int flags) {
    assert(fd == 42 && flags == 0 && sent_length + length <= sizeof(sent));
    size_t n = length > 3 ? 3 : length;
    memcpy(sent + sent_length, bytes, n);
    sent_length += n;
    return (ssize_t)n;
}
ssize_t test_recv(int fd, void *bytes, size_t length, int flags) {
    assert(fd == 42 && flags == 0);
    if (truncated || wire_position == wire_length) return 0;
    size_t n = wire_length - wire_position;
    if (n > length) n = length;
    if (n > 2) n = 2;
    memcpy(bytes, wire + wire_position, n);
    wire_position += n;
    return (ssize_t)n;
}

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
    io_port_out(2, IO_SERVICE_HTTP);
    io_port_out(3, (uint8_t)length);
    io_port_out(3, (uint8_t)(length >> 8));
    for (size_t i = 0; i < length; ++i) io_port_out(3, data[i]);
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
static void process(const ft_request_t *request, size_t n, bool eof) {
    wire[0] = eof ? FT_PROTO_RESP_EOF : FT_PROTO_RESP_OK;
    wire[1] = (uint8_t)n;
    for (size_t i = 0; i < n; ++i) wire[i + 2] = (uint8_t)(request->offset + i);
    wire_length = n + 2;
    wire_position = sent_length = 0;
    ft_process_request(request);
    if (connected && socket_ok && !truncated && request->type == FT_REQ_GET_CHUNK) {
        assert(sent[0] == FT_PROTO_GET_CHUNK);
        for (unsigned i = 0; i < 4; ++i)
            assert(sent[i + 1] == (uint8_t)(request->offset >> (8 * i)));
        assert(!strcmp((char *)sent + 5, request->filename));
    }
}
static ft_request_t take_request(void) {
    ft_request_t request;
    assert(xQueueReceive(s_ft_request_queue, &request, 0) == pdTRUE);
    return request;
}
static void read_chunk(size_t n, bool eof) {
    const uint8_t read[] = {FT_CMD_REQUEST_CHUNK};
    send_request(read, 1);
    assert(io_port_in(2) == IO_SERVICE_BUSY);
    ft_request_t request = take_request();
    process(&request, n, eof);
    assert(io_port_in(2) == IO_SERVICE_RESPONSE_READY);
    assert(io_port_in(4) == (uint8_t)(n + 1));
    assert(io_port_in(4) == (uint8_t)((n + 1) >> 8));
    assert(io_port_in(4) == (eof ? FT_STATUS_EOF : FT_STATUS_IDLE));
    for (size_t i = 0; i < n; ++i)
        assert(io_port_in(4) == (uint8_t)(request.offset + i));
    assert(io_port_in(2) == IO_SERVICE_READY);
}
static void error_reply(void) {
    assert(io_port_in(2) == IO_SERVICE_ERROR);
    assert(io_port_in(4) == 1 && io_port_in(4) == 0);
    assert(io_port_in(4) == IO_SERVICE_HTTP_TRANSFER);
    io_port_out(2, IO_SERVICE_ACK);
}

int main(void) {
    files_io_init();
    assert(io_ports_init());
    open_file("Case/File.Bin");
    read_chunk(256, false);
    read_chunk(1, false);
    read_chunk(127, true);
    const uint8_t read[] = {3};
    send_request(read, 1);
    assert(io_port_in(2) == IO_SERVICE_RESPONSE_READY);
    assert(io_port_in(4) == 1 && io_port_in(4) == 0 && io_port_in(4) == FT_STATUS_EOF);

    /* A reply completing after RESET/open cannot alter the new session. */
    open_file("old.bin");
    send_request(read, 1);
    ft_request_t old = take_request();
    io_port_out(2, IO_SERVICE_RESET);
    open_file("new.bin");
    process(&old, 256, false);
    send_request(read, 1);
    assert(io_port_in(2) == IO_SERVICE_BUSY);
    ft_request_t close = take_request();
    assert(close.type == FT_REQ_CLOSE);
    process(&close, 1, false);
    ft_request_t current = take_request();
    assert(!strcmp(current.filename, "new.bin") && current.offset == 0);
    process(&current, 1, true);
    assert(io_port_in(2) == IO_SERVICE_RESPONSE_READY);
    assert(io_port_in(4) == 2 && io_port_in(4) == 0);
    assert(io_port_in(4) == FT_STATUS_EOF && io_port_in(4) == 0);

    for (unsigned failure = 0; failure < 3; ++failure) {
        open_file("failure.bin");
        send_request(read, 1);
        current = take_request();
        connected = failure != 0;
        socket_ok = failure != 1;
        truncated = failure == 2;
        process(&current, 1, true);
        error_reply();
        connected = socket_ok = true;
        truncated = false;
        ft_disconnect();
    }
    open_file("recovered.bin");
    read_chunk(1, true);
    open_file("full-final.bin");
    read_chunk(256, true);
    char longest[FT_MAX_FILENAME];
    memset(longest, 'A', sizeof(longest) - 1);
    longest[sizeof(longest) - 1] = 0;
    open_file(longest);
    read_chunk(1, true);

    files_output(60, FT_CMD_SET_FILENAME, NULL, 0);
    for (unsigned i = 0; i < FT_MAX_FILENAME; ++i)
        files_output(61, 'A', NULL, 0);
    files_output(61, 0, NULL, 0);
    assert(files_input(60) == FT_STATUS_ERROR);

    open_file("server-error.bin");
    send_request(read, 1);
    current = take_request();
    wire[0] = FT_PROTO_RESP_ERROR;
    wire[1] = 0;
    wire_length = 2;
    wire_position = sent_length = 0;
    ft_process_request(&current);
    error_reply();
    open_file("recovered-again.bin");
    read_chunk(1, true);
    unsigned queued = (unsigned)s_ft_request_queue->count;
    for (unsigned port = 0; port < 256; ++port) {
        if ((port & 0x17) != 0x15 && (port & 0x17) != 0x16) continue;
        io_port_out((uint8_t)port, FT_CMD_CLOSE);
        assert(io_port_in((uint8_t)port) == 0);
    }
    assert(s_ft_request_queue->count == queued);
    assert(close_calls > 0);
    ft_disconnect();
    vQueueDelete(s_ft_request_queue);
    vQueueDelete(s_ft_response_queue);
    puts("PASS: ESP32 production FT queues, wire framing, binary chunks, stale replies and failures");
    return 0;
}
