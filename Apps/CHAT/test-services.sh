#!/bin/sh
# Build/run native tests of the app's service operation payloads.
set -eu
cd "$(dirname "$0")"
mkdir -p build
cc -std=c11 -O1 -Wno-deprecated-non-prototype -x c - \
    -o build/chat-service-test <<'EOF'
#include <assert.h>
#define main chat_application_main
#include "CHATC11.C"
#undef main

static int appended, append_calls, submitted, stream_offset, fail_submit;
static int waiting;
static const char stream_text[] = "Hello\nworld\n\nnext\r!";

int bdos(int f, int v) { return 0; }
int svc_start_timer(int id, unsigned ms) { assert(id == 0); return 0; }
int svc_timer_active(int id) { return 0; }

int svc_operation_send(int group, int op, const void *payload, int len, void *response, int capacity)
{
    const char *data = payload;
    char *reply = response;
    if (group == SERVICE_GROUP_ENVIRONMENT) {
        if (op == 1) { assert(len == 0); return 0; }
        assert((unsigned char)data[0] == 4);
        assert(memcmp(data + 1, "TEST", 4) == 0);
        if (op == 2) { memcpy(reply, "value", 5); return 5; }
        assert(op == 3 && len == 10 && data[5] == 3 && data[6] == 0);
        assert(memcmp(data + 7, "abc", 3) == 0);
        return 0;
    }
    assert(group == SERVICE_GROUP_CHAT);
    if (op == 0) {
        appended = append_calls = submitted = stream_offset = 0;
        waiting = 1;
        return 0;
    }
    if (op == 2) return 0;
    if (op == 1) {
        assert(len > 0 && len <= 2047);
        assert(memchr(data, 0, len) == NULL);
        appended += len;
        append_calls++;
        return 0;
    }
    if (op == 3) { submitted++; return fail_submit ? -5 : 0; }
    if (op == 4) {
        assert(submitted && capacity == 1);
        if (waiting) { waiting = 0; reply[0] = 1; }
        else reply[0] = stream_offset < sizeof(stream_text) - 1 ? 2 : 0;
        return 1;
    }
    assert(op == 5 && len == 2 && data[0] == 0 && data[1] == 1);
    assert(capacity == 259);
    reply[0] = 2;
    reply[1] = 1;
    reply[2] = 0;
    reply[3] = stream_text[stream_offset++];
    return 4;
}

int main(void)
{
    char env[16];
    char text[1501];
    int i;
    assert(env_initialize() == 0);
    assert(env_get_value("TEST", env, sizeof(env)) == 0);
    assert(strcmp(env, "value") == 0);
    assert(env_set_value("TEST", "abc") == 0);
    memset(text, 'a', 1500);
    text[1500] = 0;
    for (i = 0; i < 3; i++)
        chat_add_message(MESSAGE_TYPE_ASSISTANT, text);
    assert(chat_send_request() == 0);
    assert(append_calls >= 3 && appended > 4094 && submitted == 1);
    assert(strcmp(response_buffer, "Hello world\n\nnext!") == 0);
    assert(!response_truncated);
    fail_submit = 1;
    assert(chat_send_request() < 0);
    puts("chat service tests passed");
    return 0;
}
EOF
./build/chat-service-test
