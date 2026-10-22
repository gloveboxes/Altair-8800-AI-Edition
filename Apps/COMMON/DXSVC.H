/* ROMLESS-SVC/1 shared service client for dcc applications.
 * DXSVC.C includes the required Z80 assembly transport and validated timer APIs.
 */
#pragma once

/* Physical I/O ports. */
#define SERVICE_COMMAND_PORT 2
#define SERVICE_REQUEST_PORT 3
#define SERVICE_RESPONSE_PORT 4
#define INTERRUPT_TIMER_PORT 7
#define FILE_COMMAND_PORT 21
#define FILE_DATA_PORT 22
#define TIMER_IO_PORT 23

/* Shared-service command groups, not physical port numbers. */
#define SERVICE_GROUP_TIMERS 16
#define SERVICE_GROUP_STOPWATCHES 17
#define SERVICE_GROUP_CLOCK 32
#define SERVICE_GROUP_UTILITY 33
#define SERVICE_GROUP_ENVIRONMENT 48
#define SERVICE_GROUP_WEATHER 64
#define SERVICE_GROUP_CHAT 80
#define SERVICE_GROUP_SENSE_HAT 96
#define SERVICE_MAX_PAYLOAD 2048

/*
 * One shared transaction may be outstanding at a time.
 * Generic helpers return a byte count (0 also means COMPLETE) or a negative
 * service/transport error. Failures print a diagnostic by default and RESET the channel;
 * RESET does not undo service side effects. Binary replies are not terminated.
 * Timer, stopwatch and random convenience helpers exit(1) after an error.
 * Timer/stopwatch helpers use independent port 23, not the shared channel.
 * Their multi-byte operations must not be interleaved with other port-23 users.
 * A fresh program may recover an unknown parser state with inp(TIMER_IO_PORT) followed
 * by outp(TIMER_IO_PORT, 0); this resets only the interface, not running timers.
 */

/**
 * Enable (nonzero) or suppress (0) console error diagnostics; return the previous
 * setting. Restore it after requests whose errors are displayed by the caller.
 * Error returns, channel recovery and fatal timer behavior are unchanged.
 */
int svc_error_output(int enabled);

/**
 * Begin a low-level request to a service group, writing its length prefix.
 * request_length includes the operation byte (1..SERVICE_MAX_PAYLOAD).
 * Returns 0 or a negative error. On success, write exactly request_length bytes using
 * outp(SERVICE_REQUEST_PORT, byte), then call svc_finish_response(). Prefer svc_call_service()/svc_operation_send() normally.
 */
int svc_begin_request(int service_group, int request_length);

/**
 * Finish a fully transmitted request: wait for completion and read its reply.
 * response must hold response_capacity bytes (0..SERVICE_MAX_PAYLOAD).
 * NULL is allowed only when response_capacity is 0.
 * Returns the reply length, 0 for COMPLETE, or a negative error. An oversized
 * reply fills the available space, drains the excess, then returns -19.
 * Error replies are drained without copying into reply. Polling is limited to
 * 6,553,500 status reads, not a calibrated wall-clock timeout.
 */
int svc_finish_response(void *response, int response_capacity);

/**
 * Send a complete raw payload and receive a binary reply through svc_finish_response().
 * request is non-NULL and contains request_length bytes (1..SERVICE_MAX_PAYLOAD),
 * starting with the operation byte. Response rules follow svc_finish_response().
 */
int svc_call_service(int service_group, const void *request, int request_length,
                 void *response, int response_capacity);

/**
 * Send an operation followed by request_length bytes, without a combined buffer.
 * request_length is 0..SERVICE_MAX_PAYLOAD-1; request may be NULL only when
 * request_length is 0.
 * Response rules and return values follow svc_finish_response().
 */
int svc_operation_send(int service_group, int operation, const void *request,
                   int request_length, void *response, int response_capacity);

/**
 * Send an operation with no extra data and receive locally NUL-terminated text.
 * response is non-NULL; response_capacity includes the terminator
 * (1..SERVICE_MAX_PAYLOAD+1).
 * Returns the received byte count excluding NUL, or a negative error.
 * After a failed transaction, a valid destination is cleared to an empty string.
 */
int svc_get_text(int service_group, int operation, char *response,
             int response_capacity);

/**
 * Send [operation, one-byte selector] and receive NUL-terminated text.
 * Response rules, return values and transaction-failure clearing follow svc_get_text().
 * Valid operation/selector combinations depend on the selected service group.
 */
int svc_query_text(int service_group, int operation, int selector, char *response,
                int response_capacity);

/**
 * Start or replace countdown timer_id: IDs 0..2 use duration in milliseconds
 * (0..65535); ID 3 uses whole seconds (0..255). Zero expires immediately.
 * Returns 0 once armed; reports an error and exits(1) on failure.
 * Uses 3 OUTs for IDs 0..2 and 2 OUTs for ID 3, with no worker/status polling.
 */
int svc_start_timer(int timer_id, unsigned duration);

/**
 * Query timer_id (0..3): returns 1 while running, 0 when expired or unset.
 * Reports an error and exits(1) on failure or an invalid reply.
 * Uses one OUT to select the timer and one IN; works while services are BUSY.
 */
int svc_timer_active(int timer_id);

/**
 * Access stopwatch_id (0..2): operation 0 resets it, 1 reads elapsed seconds,
 * and 2 reads elapsed milliseconds. For operation 0, response may be NULL;
 * returns 0. For operation 1/2, response must hold four bytes; returns 4.
 * Those bytes are an unsigned little-endian 32-bit count, not text;
 * decode them explicitly.
 * Reads do not reset the stopwatch. Reports an error and exits(1) on failure.
 * Reset uses one OUT; reads use one OUT plus four INs from port 23.
 * Replies are latched at the command, so all four bytes share one timestamp.
 */
int svc_access_stopwatch(int stopwatch_id, int operation, void *response);

/**
 * Return a random unsigned 16-bit value (0..65535); zero is a valid result.
 * Reports an error and exits(1) on failure or an invalid reply length.
 */
unsigned svc_random_u16(void);
