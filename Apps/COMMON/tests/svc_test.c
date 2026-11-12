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
static unsigned char sound_log[64], sound_status[SND_STATUS_BYTES];
static unsigned sound_outs, sound_ins, sound_cursor, sound_framed;

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

static void sound_prepare(const unsigned char *status)
{
    sound_outs = sound_ins = 0;
    sound_cursor = SND_STATUS_BYTES;
    if (status)
        memcpy(sound_status, status, SND_STATUS_BYTES);
}

static void sound_expect(const unsigned char *bytes, unsigned count, unsigned ins)
{
    assert(sound_outs == count && sound_ins == ins);
    assert(!count || memcmp(sound_log, bytes, count) == 0);
    sound_outs = sound_ins = 0;
}

#define SOUND_EXPECT(ins, ...) \
    sound_expect((const unsigned char[]){__VA_ARGS__}, \
                 sizeof((const unsigned char[]){__VA_ARGS__}), ins)

void outp(unsigned port, unsigned value)
{
    if (port == SOUND_PORT) {
        assert(value < 256);
        if (sound_framed && value == 0) {
            sound_cursor = 0;
        } else {
            assert(sound_outs < sizeof(sound_log));
            sound_log[sound_outs++] = value;
        }
        sound_framed = 0;
        return;
    }
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
    if (port == SOUND_PORT) {
        sound_ins++;
        sound_framed = 1;
        return sound_cursor < SND_STATUS_BYTES ? sound_status[sound_cursor++] : 255;
    }
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

static void sound_tests(void)
{
    static const unsigned short notes[] = {262, 100, 330, 100, 392, 200};
    static const unsigned short steps[] = {1000, 50, 250, 60};
    unsigned char status[SND_STATUS_BYTES], copy[SND_STATUS_BYTES];
    int index;

    for (index = 0; index < SND_STATUS_BYTES; ++index)
        status[index] = 100 + index;
    sound_prepare(status);
    assert(snd_tone(5, 1000, 100) == 1);
    SOUND_EXPECT(1, 16, 1, 5, 0, 100, 100, 1, 3, 232, 0, 100);
    snd_options(SND_ENQUEUE, 150, 50);
    assert(snd_sequence(9, 3, 2, notes) == 2);
    SOUND_EXPECT(1, 16, 2, 9, 1, 150, 50, 4, 3, 0, 2,
                 1, 6, 0, 100, 1, 74, 0, 100, 1, 136, 0, 200);
    snd_options(0, 100, 100);
    assert(snd_segments(1, 2, 0, 500, steps) == 3);
    SOUND_EXPECT(1, 16, 3, 1, 0, 100, 100, 5, 2, 0, 0, 1, 244, 3, 232, 0, 50, 0, 250, 0, 60);
    assert(snd_sweep(0, 200, 40000u, 300) == 4);
    SOUND_EXPECT(1, 16, 4, 0, 0, 100, 100, 2, 0, 200, 156, 64, 1, 44);
    assert(snd_siren(2, 600, 1200, 400, 300, 5000) == 5);
    SOUND_EXPECT(1, 16, 5, 2, 0, 100, 100, 3, 2, 88, 4, 176, 1, 144, 1, 44, 19, 136);
    assert(snd_pulses(3, 12, 880, 20, 200, 30) == 6);
    SOUND_EXPECT(1, 16, 6, 3, 0, 100, 100, 6, 12, 3, 112, 0, 20, 0, 200, 0, 30);
    assert(snd_vibrato(4, 700, 900, 2000, 40, 60) == 7);
    SOUND_EXPECT(1, 16, 7, 4, 0, 100, 100, 7, 2, 188, 3, 132, 7, 208, 0, 40, 0, 60);
    assert(snd_arpeggio(5, 1047, 100, 1200, 1) == 8);
    SOUND_EXPECT(1, 16, 8, 5, 0, 100, 100, 8, 4, 23, 0, 100, 4, 176, 1);
    assert(snd_gate(6, 440, 50, 25, 600) == 9);
    SOUND_EXPECT(1, 16, 9, 6, 0, 100, 100, 9, 1, 184, 0, 50, 0, 25, 2, 88);
    assert(snd_preset(255, SND_COIN) == 10);
    SOUND_EXPECT(1, 16, 10, 255, 0, 100, 100, 10, 1);
    assert(snd_fx(1, 0, 1000, 100) == 0 && snd_fx(1, 11, 1) == 0);
    sound_expect(NULL, 0, 0);
    assert(snd_update(10, SND_PITCH | SND_MODULATION, 120, 100, 30, 80) == 11);
    SOUND_EXPECT(1, 18, 11, 10, 5, 120, 100, 0, 30, 0, 80);
    assert(snd_clear() == 12);
    SOUND_EXPECT(1, 19, 12);
    for (index = 13; index <= 255; ++index) {
        assert(snd_stop() == index);
        sound_outs = sound_ins = 0;
    }
    assert(snd_stop() == 1);
    SOUND_EXPECT(1, 17, 1);

    snd_read(copy);
    assert(memcmp(copy, status, SND_STATUS_BYTES) == 0);
    sound_expect(NULL, 0, 20);
    assert(snd_busy() == status[SND_S_ACTIVE_ID]);
    sound_expect(NULL, 0, 4);
    status[SND_S_EVENT_SEQUENCE] = 0x12;
    status[SND_S_EVENT_SEQUENCE + 1] = 0x34;
    status[SND_S_EVENT_ID] = 7;
    status[SND_S_EVENT_TYPE] = SND_COMPLETED;
    sound_prepare(status);
    index = snd_event();
    assert(SND_EVENT_TYPE(index) == SND_COMPLETED && SND_EVENT_ID(index) == 7);
    SOUND_EXPECT(16, 20, 0x12, 0x34);
    status[SND_S_EVENT_TYPE] = 0;
    sound_prepare(status);
    assert(snd_event() == 0);
    sound_expect(NULL, 0, 16);
    status[SND_S_ACTIVE_ID] = 0;
    sound_prepare(status);
    snd_wait();
    sound_expect(NULL, 0, 4);

    status[SND_S_VERSION] = SOUND_PROTOCOL_VERSION;
    sound_prepare(status);
    snd_options(SND_ENQUEUE, 50, 50);
    assert(snd_init() == 1);
    SOUND_EXPECT(19, 17, 2);
    assert(snd_tone(0, 100, 10) == 3);
    SOUND_EXPECT(1, 16, 3, 0, 0, 100, 100, 1, 0, 100, 0, 10);
    memset(status, 255, sizeof(status));
    sound_prepare(status);
    assert(snd_init() == 0);
    sound_expect(NULL, 0, 2);
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

    sound_tests();
    puts("CP/M shared-service client tests passed");
    return 0;
}
