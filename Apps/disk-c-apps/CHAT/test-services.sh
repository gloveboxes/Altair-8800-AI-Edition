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
static const char *provider = "compatible", *thinking;
static const char *env_read_failure;
static int env_init_failure, env_write_failure, thinking_reads, thinking_writes;
static char sent_request[REQUEST_BUFFER_LENGTH];
static const char stream_text[] = "Hello\nworld\n\nnext\r!";

int bdos(int f, int v) { return 0; }
int svc_start_timer(int id, unsigned ms) { assert(id == 0); return 0; }
int svc_timer_active(int id) { return 0; }

int svc_operation_send(int group, int op, const void *payload, int len, void *response, int capacity)
{
    const char *data = payload;
    char *reply = response;
    if (group == SERVICE_GROUP_ENVIRONMENT) {
        char key[17];
        const char *value;
        if (op == 1) { assert(len == 0); return env_init_failure; }
        assert((unsigned char)data[0] > 0 && (unsigned char)data[0] <= 16);
        memcpy(key, data + 1, (unsigned char)data[0]);
        key[(unsigned char)data[0]] = 0;
        if (op == 2 && strcmp(key, "TEST") != 0) {
            if (strcmp(key, "CHAT_THINK") == 0) thinking_reads++;
            if (env_read_failure && strcmp(key, env_read_failure) == 0) return -17;
            if (strcmp(key, "CHAT_PROVIDER") == 0) value = provider;
            else {
                assert(strcmp(key, "CHAT_THINK") == 0);
                value = thinking;
            }
            if (!value) return ENV_ERROR_NOT_FOUND;
            assert(strlen(value) <= capacity);
            memcpy(reply, value, strlen(value));
            return strlen(value);
        }
        if (op == 3 && strcmp(key, "CHAT_THINK") == 0) {
            assert(len == 18 && data[11] == 5 && data[12] == 0);
            assert(memcmp(data + 13, "false", 5) == 0);
            thinking_writes++;
            if (env_write_failure) return env_write_failure;
            thinking = "false";
            return 0;
        }
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
        assert(appended + len < sizeof(sent_request));
        memcpy(sent_request + appended, data, len);
        appended += len;
        sent_request[appended] = 0;
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

static void check_request(const char *selected_provider, const char *selected_thinking,
                          const char *expected_effort)
{
    char json[REQUEST_BUFFER_LENGTH], exact[REQUEST_BUFFER_LENGTH];
    int length, reads_before = thinking_reads, writes_before = thinking_writes;
    provider = selected_provider;
    thinking = selected_thinking;
    assert(chat_load_thinking_config() == 0);
    length = json_generate_chat_request("system\n", NULL, NULL, 0, json, sizeof(json));
    assert(length == strlen(json));
    snprintf(exact, sizeof(exact),
             "{\"model\":\"gemma3:1b\",\"messages\":[{\"role\":\"system\",\"content\":\"system\\n\"}],"
             "\"max_tokens\":1024,\"temperature\":0.7,\"stream\":true%s%s%s}",
             expected_effort ? ",\"reasoning_effort\":\"" : "",
             expected_effort ? expected_effort : "", expected_effort ? "\"" : "");
    assert(strcmp(json, exact) == 0);
    if (!selected_provider || strcmp(selected_provider, "openai") == 0 ||
        selected_provider[0] == 0) {
        assert(thinking_reads == reads_before && thinking_writes == writes_before);
    }
    {
        char guarded[REQUEST_BUFFER_LENGTH + 2];
        memset(guarded, '!', sizeof(guarded));
        assert(json_generate_chat_request("system\n", NULL, NULL, 0,
                                         guarded + 1, length + 1) == length);
        assert(guarded[0] == '!' && guarded[length + 2] == '!');
        memset(guarded, '!', sizeof(guarded));
        assert(json_generate_chat_request("system\n", NULL, NULL, 0,
                                         guarded + 1, length) < 0);
        assert(guarded[0] == '!' && guarded[length + 1] == '!');
    }
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
    assert(chat_initialize() == 0);
    check_request("compatible", NULL, "none");
    assert(thinking_writes == 1 && strcmp(thinking, "false") == 0);
    check_request("compatible", "", "none");
    check_request("compatible", "false", "none");
    check_request("compatible", "true", "medium");
    check_request("compatible", "default", NULL);
    check_request("ollama", NULL, "none");
    check_request("ollama", "true", "medium");
    check_request("openai", "false", NULL);
    check_request("openai", "true", NULL);
    check_request("openai", "invalid", NULL);
    check_request("", "false", NULL);
    check_request(NULL, "false", NULL);
    provider = "compatible";
    thinking = "invalid";
    assert(chat_send_request() < 0 && submitted == 0);
    env_read_failure = "CHAT_PROVIDER";
    assert(chat_send_request() < 0 && submitted == 0);
    env_read_failure = "CHAT_THINK";
    assert(chat_send_request() < 0 && submitted == 0);
    env_read_failure = NULL;
    env_init_failure = -17;
    assert(chat_load_environment() < 0);
    env_init_failure = 0;
    thinking = NULL;
    env_write_failure = -18;
    assert(chat_send_request() < 0 && submitted == 0);
    env_write_failure = 0;
    thinking = "false";
    memset(text, 'a', 1500);
    text[1500] = 0;
    for (i = 0; i < 3; i++)
        chat_add_message(MESSAGE_TYPE_ASSISTANT, text);
    assert(chat_send_request() == 0);
    assert(append_calls >= 3 && appended > 4094 && submitted == 1);
    assert(strstr(sent_request, "\"reasoning_effort\":\"none\"") != NULL);
    assert(strcmp(response_buffer, "Hello world\n\nnext!") == 0);
    assert(!response_truncated);
    thinking = "true";
    assert(chat_send_request() == 0);
    assert(strstr(sent_request, "\"reasoning_effort\":\"medium\"") != NULL);
    provider = "openai";
    assert(chat_send_request() == 0);
    assert(strstr(sent_request, "\"reasoning_effort\"") == NULL);
    fail_submit = 1;
    assert(chat_send_request() < 0);
    puts("chat service tests passed");
    return 0;
}
EOF
./build/chat-service-test
