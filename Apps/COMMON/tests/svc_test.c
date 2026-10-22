#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../DXSVC.H"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

static int state, final_state, group, resets, acknowledgements;
static unsigned request_size, request_pos, reply_size, reply_pos, busy, status_reads;
static unsigned char request[SERVICE_MAX_PAYLOAD + 2], reply[SERVICE_MAX_PAYLOAD];
static char diagnostic[80];
static unsigned diagnostic_size;
static unsigned char timer_writes[3];
static unsigned timer_outs, timer_ins;
static jmp_buf termination;
static int exit_expected;

void svc_test_exit(int code)
{
    assert(exit_expected && code == 1);
    longjmp(termination, 1);
}

int putchar(int byte)
{
    assert(diagnostic_size + 1 < sizeof(diagnostic));
    diagnostic[diagnostic_size++] = byte;
    diagnostic[diagnostic_size] = 0;
    return byte;
}

static void prepare(int status, const unsigned char *data, unsigned length)
{
    state = 0;
    final_state = status;
    request_size = request_pos = reply_pos = 0;
    reply_size = length;
    resets = acknowledgements = 0;
    status_reads = 0;
    busy = 3;
    diagnostic_size = 0;
    diagnostic[0] = 0;
    timer_outs = timer_ins = 0;
    if (length)
        memcpy(reply, data, length);
}

void outp(unsigned port, unsigned value)
{
    if (port == TIMER_IO_PORT) {
        assert(timer_outs < sizeof(timer_writes));
        timer_writes[timer_outs++] = value & 255;
        return;
    }
    if (port == SERVICE_COMMAND_PORT) {
        if (value == 1) {
            resets++;
            state = 0;
        } else if (value == 0) {
            assert(state == 1);
            acknowledgements++;
            state = 0;
        } else {
            assert(state == 0);
            group = value;
            state = 3;
        }
    } else {
        assert(port == SERVICE_REQUEST_PORT && state == 3);
        assert(request_pos < sizeof(request));
        request[request_pos++] = value & 255;
        if (request_pos == 2)
            request_size = request[0] | ((unsigned)request[1] << 8);
        if (request_pos >= 2 && request_pos == request_size + 2)
            state = final_state;
    }
}

int inp(unsigned port)
{
    int value;
    if (port == TIMER_IO_PORT) {
        assert(timer_ins < reply_size);
        return reply[timer_ins++];
    }
    if (port == SERVICE_COMMAND_PORT) {
        status_reads++;
        if (state != 0 && state != 3 && busy) {
            busy--;
            return 4;
        }
        return state;
    }
    assert(port == SERVICE_RESPONSE_PORT && (state == 2 || state == 255));
    assert(reply_pos < reply_size + 2);
    if (reply_pos == 0)
        value = reply_size & 255;
    else if (reply_pos == 1)
        value = reply_size >> 8;
    else
        value = reply[reply_pos - 2];
    reply_pos++;
    if (reply_pos == reply_size + 2 && state == 2)
        state = 0;
    return value;
}

static void invalid_timer(int which, int id, unsigned delay, int operation,
                          void *buffer, int code)
{
    char expected[80];
    prepare(1, NULL, 0);
    state = 4;
    exit_expected = 1;
    if (setjmp(termination) == 0) {
        if (which == 0)
            svc_start_timer(id, delay);
        else if (which == 1)
            svc_timer_active(id);
        else
            svc_access_stopwatch(id, operation, buffer);
        assert(!"Invalid timer argument did not terminate");
    }
    exit_expected = 0;
    assert(timer_outs == 0 && timer_ins == 0 && request_pos == 0);
    assert(state == 4 && resets == 0 && status_reads == 0);
    snprintf(expected, sizeof(expected), "\r\nService I/O error %d\r\n", code);
    assert(strcmp(diagnostic, expected) == 0);
}

int main(void)
{
    const unsigned char bytes[] = {0, 128, 255, 0};
    const unsigned char random_bytes[] = {0, 128};
    const unsigned char error[] = {20};
    unsigned char error_data[3] = {0, 128, 255};
    char buffer[8], data[2], body[SERVICE_MAX_PAYLOAD], large[SERVICE_MAX_PAYLOAD];
    char expected[80];
    unsigned index;

    prepare(255, error, 1);
    assert(svc_error_output(0) == 1);
    assert(svc_query_text(SERVICE_GROUP_WEATHER, 0, 0, buffer, sizeof buffer) == -20);
    assert(buffer[0] == 0 && diagnostic_size == 0);
    assert(reply_pos == 3 && resets == 1 && state == 0);
    assert(svc_error_output(1) == 0);
    prepare(255, error, 1);
    assert(svc_query_text(SERVICE_GROUP_WEATHER, 0, 0, buffer, sizeof buffer) == -20);
    assert(strcmp(diagnostic, "\r\nService I/O error 20\r\n") == 0);

    prepare(2, bytes, sizeof(bytes));
    data[0] = 9;
    data[1] = 0;
    assert(svc_call_service(SERVICE_GROUP_CLOCK, data, 2, buffer, 8) == 4);
    assert(memcmp(buffer, bytes, 4) == 0 && state == 0);
    assert(group == SERVICE_GROUP_CLOCK && request_size == 2);
    assert(request[2] == 9 && request[3] == 0);

    prepare(1, NULL, 0);
    assert(svc_start_timer(2, 65535) == 0);
    assert(timer_outs == 3 && timer_ins == 0);
    assert(memcmp(timer_writes, "\x12\xff\xff", 3) == 0);
    assert(request_pos == 0 && status_reads == 0 && acknowledgements == 0);
    prepare(1, NULL, 0);
    assert(svc_start_timer(3, 255) == 0);
    assert(timer_outs == 2 && timer_writes[0] == 0x13 && timer_writes[1] == 255);
    prepare(2, (const unsigned char *)"\1", 1);
    assert(svc_timer_active(3) == 1);
    assert(timer_outs == 1 && timer_ins == 1 && timer_writes[0] == 0x23);

    prepare(2, random_bytes, 2);
    assert(svc_random_u16() == 32768);

    prepare(2, bytes, 4);
    assert(svc_access_stopwatch(1, 2, buffer) == 4);
    assert(timer_outs == 1 && timer_ins == 4 && timer_writes[0] == 0x61);
    assert(memcmp(buffer, bytes, 4) == 0);
    assert(request_pos == 0 && status_reads == 0);
    prepare(1, NULL, 0);
    assert(svc_access_stopwatch(2, 0, NULL) == 0);
    assert(timer_outs == 1 && timer_ins == 0 && timer_writes[0] == 0x42);
    invalid_timer(0, -1, 1, 0, NULL, 4);
    invalid_timer(0, 4, 1, 0, NULL, 4);
    invalid_timer(0, 3, 256, 0, NULL, 4);
    invalid_timer(1, -1, 0, 0, NULL, 4);
    invalid_timer(1, 4, 0, 0, NULL, 4);
    invalid_timer(2, -1, 0, 0, NULL, 4);
    invalid_timer(2, 3, 0, 0, NULL, 4);
    invalid_timer(2, 0, 0, -1, buffer, 4);
    invalid_timer(2, 0, 0, 3, buffer, 4);
    invalid_timer(2, 0, 0, 1, NULL, 3);
    invalid_timer(2, 0, 0, 2, NULL, 3);

    prepare(2, (const unsigned char *)"abc", 3);
    assert(svc_query_text(SERVICE_GROUP_WEATHER, 0, 14, buffer, 8) == 3);
    assert(strcmp(buffer, "abc") == 0);
    assert(request[2] == 0 && request[3] == 14);

    prepare(2, NULL, 0);
    buffer[0] = 'x';
    assert(svc_get_text(SERVICE_GROUP_ENVIRONMENT, 5, buffer, 8) == 0 && buffer[0] == 0);
    assert(state == 0);

    prepare(2, bytes, 4);
    assert(svc_call_service(SERVICE_GROUP_UTILITY, data, 2, buffer, 2) == -19);
    assert(reply_pos == reply_size + 2 && resets == 1 && state == 0);

    prepare(255, error, 1);
    assert(svc_call_service(SERVICE_GROUP_ENVIRONMENT, data, 2, buffer, 8) == -20);
    assert(reply_pos == 3 && resets == 1 && state == 0);

    prepare(1, NULL, 0);
    assert(svc_operation_send(SERVICE_GROUP_CHAT, 1, data, 2, buffer, 8) == 0);
    assert(request_size == 3 && request[2] == 1 && request[3] == 9);
    assert(request[4] == 0);

    prepare(1, NULL, 0);
    assert(svc_call_service(SERVICE_GROUP_ENVIRONMENT, data, 0, buffer, 8) == -3);
    assert(resets == 1);
    prepare(1, NULL, 0);
    assert(svc_operation_send(SERVICE_GROUP_ENVIRONMENT, 1, data, -1, buffer, 8) == -3);

    prepare(1, NULL, 0);
    assert(svc_call_service(SERVICE_GROUP_ENVIRONMENT, data, 1, NULL, 1) == -3);
    assert(request_pos == 0 && resets == 1);
    prepare(1, NULL, 0);
    assert(svc_operation_send(SERVICE_GROUP_ENVIRONMENT, 1, NULL, 1, buffer, 8) == -3);
    assert(request_pos == 0 && resets == 1);
    prepare(1, NULL, 0);
    assert(svc_get_text(SERVICE_GROUP_ENVIRONMENT, 1, NULL, 8) == -3);
    assert(request_pos == 0 && resets == 1);

    for (index = 0; index < SERVICE_MAX_PAYLOAD; index++)
        body[index] = index & 255;
    prepare(1, NULL, 0);
    assert(svc_operation_send(SERVICE_GROUP_CHAT, 1, body, SERVICE_MAX_PAYLOAD - 1, buffer, 8) == 0);
    assert(request_size == SERVICE_MAX_PAYLOAD && request_pos == SERVICE_MAX_PAYLOAD + 2);
    assert(request[0] == 0 && request[1] == 8 && request[2] == 1);
    assert(memcmp(request + 3, body, SERVICE_MAX_PAYLOAD - 1) == 0);
    prepare(1, NULL, 0);
    assert(svc_operation_send(SERVICE_GROUP_CHAT, 1, body, SERVICE_MAX_PAYLOAD, buffer, 8) == -3);
    assert(request_pos == 0 && resets == 1);
    prepare(2, (const unsigned char *)body, SERVICE_MAX_PAYLOAD);
    assert(svc_call_service(SERVICE_GROUP_CHAT, data, 1, large, SERVICE_MAX_PAYLOAD) == SERVICE_MAX_PAYLOAD);
    assert(memcmp(large, body, SERVICE_MAX_PAYLOAD) == 0 && state == 0);

    prepare(2, bytes, 4);
    assert(svc_call_service(SERVICE_GROUP_UTILITY, data, 1, NULL, 0) == -19);
    assert(reply_pos == 6 && resets == 1);
    prepare(255, NULL, 0);
    assert(svc_call_service(SERVICE_GROUP_ENVIRONMENT, data, 1, buffer, 8) == -8);
    for (index = 0; index <= 255; ++index) {
        error_data[0] = index;
        prepare(255, error_data, 3);
        assert(svc_call_service(SERVICE_GROUP_ENVIRONMENT, data, 1, buffer, 8) == -(int)index);
        assert(reply_pos == 5 && resets == 1);
        snprintf(expected, sizeof(expected), "\r\nService I/O error %u\r\n", index);
        assert(strcmp(diagnostic, expected) == 0);
    }

    prepare(1, NULL, 0);
    busy = 65535;
    assert(svc_call_service(SERVICE_GROUP_ENVIRONMENT, data, 1, buffer, 8) == 0);
    assert(status_reads == 65538 && acknowledgements == 1);
    prepare(1, NULL, 0);
    busy = 6553499;
    assert(svc_call_service(SERVICE_GROUP_ENVIRONMENT, data, 1, buffer, 8) == 0);
    assert(status_reads == 6553502 && acknowledgements == 1);
    prepare(1, NULL, 0);
    busy = 6553500;
    assert(svc_call_service(SERVICE_GROUP_ENVIRONMENT, data, 1, buffer, 8) == -7);
    assert(status_reads == 6553502 && resets == 1 && state == 0);

    puts("CP/M shared-service client tests passed");
    return 0;
}
