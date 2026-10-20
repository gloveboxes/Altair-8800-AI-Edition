#include "io_service.h"
#include "chat_io.h"
#include "environment_io.h"
#include "time_io.h"
#include "utility_io.h"
#include "weather_io.h"

#include <string.h>
#include <time.h>
#include <stdatomic.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#else
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
#endif

#ifdef ALTAIR_LOCAL_SERVICE_SENSE_HAT
#include "drivers/sense_hat/sense_hat_panel.h"
#endif

static uint8_t request[IO_SERVICE_MAX_PAYLOAD];
static size_t request_length;
static uint8_t command;
static io_service_result_t result;
static bool initialized;
static bool pending;
static atomic_bool done;
static size_t chat_length;

#ifdef ESP_PLATFORM
static const char *TAG = "IO_SERVICE";
static QueueHandle_t requests;
static QueueHandle_t responses;
static TaskHandle_t worker;
#else
static atomic_bool stopping;
#ifdef _WIN32
static HANDLE wake_event;
static HANDLE worker;
#else
static pthread_t worker;
static pthread_mutex_t wake_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake_condition = PTHREAD_COND_INITIALIZER;
static bool requested;
#endif
#endif

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static io_service_error_t text_result(size_t length)
{
    if (length > IO_SERVICE_MAX_PAYLOAD)
        return IO_SERVICE_CAPACITY;
    if (length && result.data[length - 1] == 0)
        --length;
    result.length = length;
    result.has_response = true;
    return IO_SERVICE_OK;
}

static io_service_error_t byte_result(uint8_t value)
{
    result.data[0] = value;
    result.length = 1;
    result.has_response = true;
    return IO_SERVICE_OK;
}

static io_service_error_t clock_service(void)
{
    static const uint8_t ports[] = {41, 42, 43, 44, 31};
    if (request[0] >= sizeof(ports))
        return IO_SERVICE_UNKNOWN_OPERATION;
    if (request_length != 1)
        return IO_SERVICE_INVALID_LENGTH;
    /* A boot-relative libc clock is not synchronized calendar time. */
    if (request[0] != 0 && time(NULL) < (time_t)1704067200)
        return IO_SERVICE_CLOCK_UNSYNCED;
    size_t length = time_output(ports[request[0]], 0,
                                (char *)result.data, sizeof(result.data));
    if (length == 0 || result.data[0] == '+')
        return IO_SERVICE_UNAVAILABLE;
    return text_result(length);
}

static io_service_error_t utility_service(void)
{
    uint8_t operation = request[0];
    if (operation > 3)
        return IO_SERVICE_UNKNOWN_OPERATION;
    if (request_length != ((operation == 1 || operation == 2) ? 2u : 1u))
        return IO_SERVICE_INVALID_LENGTH;
    if (operation == 1 && request[1] > 2)
        return IO_SERVICE_INVALID_PARAMETER;
    if (operation == 2)
    {
        if (request[1] != 0xa5)
            return IO_SERVICE_REBOOT_CONFIRMATION;
#ifdef ESP_PLATFORM
        return IO_SERVICE_OK;
#else
        return IO_SERVICE_UNAVAILABLE;
#endif
    }
    static const uint8_t ports[] = {45, 48, 49, 70};
    size_t length = utility_output(ports[operation],
                                   operation == 1 ? request[1] : 0,
                                   (char *)result.data, sizeof(result.data));
    if (operation == 0)
    {
        if (length != 2)
            return IO_SERVICE_UNAVAILABLE;
        result.has_response = true;
        result.length = length;
        return IO_SERVICE_OK;
    }
    return text_result(length);
}

static void env_byte(uint8_t byte)
{
    environment_output(ENVIRONMENT_PORT_DATA, byte, NULL, 0);
}

static io_service_error_t environment_service(void)
{
    uint8_t operation = request[0];
    size_t key_length = 0;
    size_t value_length = 0;
    bool has_key = operation == 2 || operation == 3 ||
                   operation == 4 || operation == 8;
    if (operation > 9)
        return IO_SERVICE_UNKNOWN_OPERATION;
    if (has_key)
    {
        if (request_length < 2)
            return IO_SERVICE_INVALID_LENGTH;
        key_length = request[1];
        if (key_length == 0 || key_length > 16)
            return IO_SERVICE_INVALID_PARAMETER;
        if (request_length < 2 + key_length)
            return IO_SERVICE_INVALID_LENGTH;
        if (memchr(request + 2, 0, key_length))
            return IO_SERVICE_INVALID_PARAMETER;
        if (operation == 3)
        {
            if (request_length < 4 + key_length)
                return IO_SERVICE_INVALID_LENGTH;
            value_length = read_u16(request + 2 + key_length);
            if (value_length > 255)
                return IO_SERVICE_INVALID_PARAMETER;
            if (request_length != 4 + key_length + value_length)
                return IO_SERVICE_INVALID_LENGTH;
            if (memchr(request + 4 + key_length, 0, value_length))
                return IO_SERVICE_INVALID_PARAMETER;
        }
        else if (request_length != 2 + key_length)
            return IO_SERVICE_INVALID_LENGTH;
    }
    else if (operation == 9)
    {
        /* The legacy request builder reserves a byte for its local NUL. */
        if (request_length > 512)
            return IO_SERVICE_INVALID_LENGTH;
        if (memchr(request + 1, 0, request_length - 1))
            return IO_SERVICE_INVALID_PARAMETER;
    }
    else if (request_length != 1)
        return IO_SERVICE_INVALID_LENGTH;

    environment_output(ENVIRONMENT_PORT_COMMAND, 0, NULL, 0);
    if (has_key)
    {
        for (size_t i = 0; i < key_length; ++i)
            env_byte(request[2 + i]);
        env_byte(0);
        if (operation == 3)
            for (size_t i = 0; i < value_length; ++i)
                env_byte(request[4 + key_length + i]);
    }
    else if (operation == 9)
        for (size_t i = 1; i < request_length; ++i)
            env_byte(request[i]);

    size_t length = environment_output(ENVIRONMENT_PORT_COMMAND, operation,
                                       (char *)result.data, sizeof(result.data));
    uint8_t status = environment_input(ENVIRONMENT_PORT_COMMAND);
    switch (status)
    {
        case 0: break;
        case 255: return IO_SERVICE_STORAGE_OPEN;
        case 254: return IO_SERVICE_STORAGE_READ;
        case 253: return IO_SERVICE_STORAGE_WRITE;
        case 252: return IO_SERVICE_CAPACITY;
        case 251: return IO_SERVICE_NOT_FOUND;
        default: return IO_SERVICE_UNAVAILABLE;
    }
    if (operation == 2 || operation == 5 || operation == 6 || operation == 9)
        return text_result(length);
    return IO_SERVICE_OK;
}

static io_service_error_t weather_service(void)
{
    if (request[0] > 1)
        return IO_SERVICE_UNKNOWN_OPERATION;
    if (request_length != (request[0] == 0 ? 2u : 1u))
        return IO_SERVICE_INVALID_LENGTH;
    uint8_t status = weather_input(47);
    if (request[0] == 1)
        return byte_result(status);
    if (request[1] > WEATHER_FIELD_FC_FEELS)
        return IO_SERVICE_INVALID_PARAMETER;
    if (request[1] != WEATHER_FIELD_UNITS && request[1] != WEATHER_FIELD_ERROR &&
        !weather_io_has_data())
        return IO_SERVICE_UNAVAILABLE;
    size_t length = weather_output(46, request[1], (char *)result.data,
                                   sizeof(result.data));
    return text_result(length);
}

static io_service_error_t chat_service(void)
{
    uint8_t operation = request[0];
    if (operation > 5)
        return IO_SERVICE_UNKNOWN_OPERATION;
    if ((operation == 5 && request_length != 3) ||
        (operation != 1 && operation != 5 && request_length != 1))
        return IO_SERVICE_INVALID_LENGTH;
    if (!chat_io_available())
        return IO_SERVICE_UNAVAILABLE;
    switch (operation)
    {
        case 0:
            chat_output(CHAT_PORT_TRIGGER, 0, NULL, 0);
            chat_length = 0;
            return IO_SERVICE_OK;
        case 1:
            if (chat_length + request_length - 1 > 8191)
                return IO_SERVICE_CAPACITY;
            if (memchr(request + 1, 0, request_length - 1))
                return IO_SERVICE_INVALID_PARAMETER;
            for (size_t i = 1; i < request_length; ++i)
                chat_output(CHAT_PORT_REQUEST, request[i], NULL, 0);
            chat_length += request_length - 1;
            return IO_SERVICE_OK;
        case 2:
            chat_output(CHAT_PORT_RESET_RESPONSE, 0, NULL, 0);
            return IO_SERVICE_OK;
        case 3:
            return (io_service_error_t)chat_service_submit();
        case 4:
            return byte_result(chat_input(CHAT_PORT_STATUS));
        case 5:
        {
            uint16_t maximum = read_u16(request + 1);
            if (maximum == 0 || maximum > 256)
                return IO_SERVICE_INVALID_PARAMETER;
            size_t count = 0;
            uint8_t status;
            while ((status = chat_input(CHAT_PORT_STATUS)) == CHAT_STATUS_DATA_READY &&
                   count < maximum)
                result.data[3 + count++] = chat_input(CHAT_PORT_DATA);
            result.data[0] = count ? CHAT_STATUS_DATA_READY : status;
            result.data[1] = (uint8_t)count;
            result.data[2] = (uint8_t)(count >> 8);
            result.length = 3 + count;
            result.has_response = true;
            return IO_SERVICE_OK;
        }
    }
    return IO_SERVICE_UNKNOWN_OPERATION;
}

static io_service_error_t sense_hat_service(void)
{
    if (request_length != 2)
        return IO_SERVICE_INVALID_LENGTH;
    uint8_t selector = request[0];
    if (selector != 63 && selector != 65 && selector != 80 &&
        selector != 81 && selector != 85 && (selector < 90 || selector > 102))
        return IO_SERVICE_UNKNOWN_OPERATION;
#ifdef ALTAIR_LOCAL_SERVICE_SENSE_HAT
    if (!sense_hat_panel_is_active())
        return IO_SERVICE_UNAVAILABLE;
    size_t length = sense_hat_panel_output(selector, request[1],
                                           (char *)result.data, sizeof(result.data));
    return selector == 63 ? text_result(length) : IO_SERVICE_OK;
#else
    return IO_SERVICE_UNAVAILABLE;
#endif
}

static void execute(void)
{
    result.error = IO_SERVICE_OK;
    result.length = 0;
    result.has_response = false;
    result.data[0] = 0;
    switch (command)
    {
        case IO_SERVICE_CLOCK: result.error = clock_service(); break;
        case IO_SERVICE_UTILITY: result.error = utility_service(); break;
        case IO_SERVICE_ENVIRONMENT: result.error = environment_service(); break;
        case IO_SERVICE_WEATHER: result.error = weather_service(); break;
        case IO_SERVICE_CHAT: result.error = chat_service(); break;
        case IO_SERVICE_SENSE_HAT: result.error = sense_hat_service(); break;
        default: result.error = IO_SERVICE_UNKNOWN_GROUP; break;
    }
}

#ifdef ESP_PLATFORM
static void service_worker(void *argument)
{
    (void)argument;
    void *job;
    while (xQueueReceive(requests, &job, portMAX_DELAY) == pdTRUE && job)
    {
        execute();
        bool reboot = command == IO_SERVICE_UTILITY && request[0] == 2 &&
                      result.error == IO_SERVICE_OK;
        xQueueSend(responses, &job, portMAX_DELAY);
        /* Publish only after the result is queued, so polling cannot miss it. */
        atomic_store_explicit(&done, true, memory_order_release);
        if (reboot)
            utility_output(49, 0xa5, NULL, 0);
    }
    job = NULL;
    xQueueSend(responses, &job, portMAX_DELAY);
    vTaskDelete(NULL);
}
#else
#ifdef _WIN32
static DWORD WINAPI service_worker(void *argument)
#else
static void *service_worker(void *argument)
#endif
{
    (void)argument;
    for (;;)
    {
#ifdef _WIN32
        WaitForSingleObject(wake_event, INFINITE);
#else
        pthread_mutex_lock(&wake_mutex);
        while (!requested && !atomic_load(&stopping))
            pthread_cond_wait(&wake_condition, &wake_mutex);
        requested = false;
        pthread_mutex_unlock(&wake_mutex);
#endif
        if (atomic_load(&stopping))
            break;
        execute();
        atomic_store_explicit(&done, true, memory_order_release);
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
#endif

bool io_service_init(void)
{
    if (initialized)
        return true;
    atomic_store_explicit(&done, false, memory_order_relaxed);
#ifdef ESP_PLATFORM
    requests = xQueueCreate(1, sizeof(void *));
    responses = xQueueCreate(1, sizeof(void *));
    if (!requests || !responses ||
        xTaskCreatePinnedToCore(service_worker, "io_service", 8192,
                                NULL, 5, &worker, 0) != pdPASS)
    {
        ESP_LOGE(TAG, "Cannot initialize service worker");
        if (requests) vQueueDelete(requests);
        if (responses) vQueueDelete(responses);
        requests = responses = NULL;
        return false;
    }
#else
    atomic_store(&stopping, false);
#ifdef _WIN32
    wake_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    worker = wake_event ? CreateThread(NULL, 0, service_worker, NULL, 0, NULL) : NULL;
    if (!worker)
    {
        if (wake_event) CloseHandle(wake_event);
        fprintf(stderr, "Cannot initialize I/O service worker\n");
        return false;
    }
#else
    requested = false;
    if (pthread_create(&worker, NULL, service_worker, NULL) != 0)
    {
        fprintf(stderr, "Cannot initialize I/O service worker\n");
        return false;
    }
#endif
#endif
    chat_length = 0;
    pending = false;
    initialized = true;
    return true;
}

void io_service_shutdown(void)
{
    if (!initialized)
        return;
#ifdef ESP_PLATFORM
    void *job = NULL;
    xQueueSend(requests, &job, portMAX_DELAY);
    do
    {
        xQueueReceive(responses, &job, portMAX_DELAY);
    } while (job);
    vQueueDelete(requests);
    vQueueDelete(responses);
#else
    atomic_store(&stopping, true);
#ifdef _WIN32
    SetEvent(wake_event);
    WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker);
    CloseHandle(wake_event);
#else
    pthread_mutex_lock(&wake_mutex);
    pthread_cond_signal(&wake_condition);
    pthread_mutex_unlock(&wake_mutex);
    pthread_join(worker, NULL);
#endif
#endif
    pending = false;
    initialized = false;
}

io_service_error_t io_service_submit(uint8_t group, const uint8_t *data,
                                    size_t length, uint32_t generation)
{
    if (!initialized)
        return IO_SERVICE_UNAVAILABLE;
    if (pending)
        return IO_SERVICE_BUSY_ERROR;
    if (length == 0 || length > sizeof(request))
        return IO_SERVICE_INVALID_LENGTH;
    memcpy(request, data, length);
    request_length = length;
    command = group;
    result.generation = generation;
    pending = true;
    atomic_store_explicit(&done, false, memory_order_relaxed);
#ifdef ESP_PLATFORM
    void *job = &result;
    if (xQueueSend(requests, &job, 0) != pdTRUE)
    {
        pending = false;
        return IO_SERVICE_BUSY_ERROR;
    }
#else
#ifdef _WIN32
    SetEvent(wake_event);
#else
    pthread_mutex_lock(&wake_mutex);
    requested = true;
    pthread_cond_signal(&wake_condition);
    pthread_mutex_unlock(&wake_mutex);
#endif
#endif
    return IO_SERVICE_OK;
}

const io_service_result_t *io_service_poll(void)
{
    if (!initialized || !pending ||
        !atomic_load_explicit(&done, memory_order_acquire))
        return NULL;
#ifdef ESP_PLATFORM
    void *job;
    if (xQueueReceive(responses, &job, 0) != pdTRUE)
        return NULL;
#endif
    pending = false;
    return &result;
}
