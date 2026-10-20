/* Stateless CP/M shared-service transport. */
#include <stdio.h>
#include <stdlib.h>
#include "DXSVC.H"

int inp(unsigned port);
void outp(unsigned port, unsigned value);
int io_wait_completion(void);
void io_write_request(const void *request, unsigned byte_count);
void io_read_response(void *response, unsigned byte_count);
void io_drain_response(unsigned byte_count);
void io_abort_timer(int error_code);

/* Native tests supply mocked primitives; dcc always uses this assembly.
 * Caller cleans arguments; HL returns int; IX/IY and interrupt state survive.
 * AF/BC/DE/HL are scratch. Ports use immediate IN/OUT, not ED block I/O.
 */
#if defined(_DCC_) && !defined(__INTELLISENSE__) && !defined(__clang__)
#asm
        public _io_wait_completion
_io_wait_completion:
        ld d,100
WAIT_BATCH:
        ld bc,65535
WAIT_POLL:
        in a,(2)
        cp 4
        jr nz,WAIT_RETURN
        dec bc
        ld a,b
        or c
        jr nz,WAIT_POLL
        dec d
        jr nz,WAIT_BATCH
        ld a,4
WAIT_RETURN:
        ld l,a
        ld h,0
        ret

        public _io_write_request
_io_write_request:
        push ix
        ld ix,0
        add ix,sp
        ld l,(ix+4)
        ld h,(ix+5)
        ld c,(ix+6)
        ld b,(ix+7)
        ld a,b
        or c
        jr z,WRITE_RETURN
WRITE_LOOP:
        ld a,(hl)
        out (3),a
        inc hl
        dec bc
        ld a,b
        or c
        jr nz,WRITE_LOOP
WRITE_RETURN:
        pop ix
        ret

        public _io_read_response
_io_read_response:
        push ix
        ld ix,0
        add ix,sp
        ld l,(ix+4)
        ld h,(ix+5)
        ld c,(ix+6)
        ld b,(ix+7)
        ld a,b
        or c
        jr z,READ_RETURN
READ_LOOP:
        in a,(4)
        ld (hl),a
        inc hl
        dec bc
        ld a,b
        or c
        jr nz,READ_LOOP
READ_RETURN:
        pop ix
        ret

        public _io_drain_response
_io_drain_response:
        push ix
        ld ix,0
        add ix,sp
        ld c,(ix+4)
        ld b,(ix+5)
        ld a,b
        or c
        jr z,DRAIN_RETURN
DRAIN_LOOP:
        in a,(4)
        dec bc
        ld a,b
        or c
        jr nz,DRAIN_LOOP
DRAIN_RETURN:
        pop ix
        ret

        public _svc_start_timer
_svc_start_timer:
        ld hl,2
        add hl,sp
        ld e,(hl)
        inc hl
        ld a,(hl)
        or a
        jp nz,TIMER_INVALID_ARGUMENT
        ld a,e
        cp 4
        jp nc,TIMER_INVALID_ARGUMENT
        inc hl
        ld c,(hl)
        inc hl
        ld b,(hl)
        cp 3
        jr c,TIMER_WRITE_DURATION
        ld a,b
        or a
        jp nz,TIMER_INVALID_ARGUMENT
TIMER_WRITE_DURATION:
        ld a,e
        add a,16
        out (23),a
        ld a,c
        out (23),a
        ld a,e
        cp 3
        jr z,TIMER_START_RETURN
        ld a,b
        out (23),a
TIMER_START_RETURN:
        ld hl,0
        ret

        public _svc_timer_active
_svc_timer_active:
        ld hl,2
        add hl,sp
        ld e,(hl)
        inc hl
        ld a,(hl)
        or a
        jp nz,TIMER_INVALID_ARGUMENT
        ld a,e
        cp 4
        jp nc,TIMER_INVALID_ARGUMENT
        add a,32
        out (23),a
        in a,(23)
        cp 2
        jr nc,TIMER_INVALID_REPLY
        ld l,a
        ld h,0
        ret
TIMER_INVALID_REPLY:
        xor a
        out (23),a
        jp TIMER_INVALID_ARGUMENT

        public _svc_access_stopwatch
_svc_access_stopwatch:
        ld hl,2
        add hl,sp
        ld e,(hl)
        inc hl
        ld a,(hl)
        or a
        jp nz,TIMER_INVALID_ARGUMENT
        ld a,e
        cp 3
        jp nc,TIMER_INVALID_ARGUMENT
        inc hl
        ld c,(hl)
        inc hl
        ld a,(hl)
        or a
        jp nz,TIMER_INVALID_ARGUMENT
        ld a,c
        cp 3
        jp nc,TIMER_INVALID_ARGUMENT
        or a
        jr z,STOPWATCH_RESET
        inc hl
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a
        or h
        jr z,TIMER_NULL_RESPONSE
        ld a,c
        rlca
        rlca
        rlca
        rlca
        add a,64
        add a,e
        out (23),a
        in a,(23)
        ld (hl),a
        inc hl
        in a,(23)
        ld (hl),a
        inc hl
        in a,(23)
        ld (hl),a
        inc hl
        in a,(23)
        ld (hl),a
        ld hl,4
        ret
STOPWATCH_RESET:
        ld a,e
        add a,64
        out (23),a
        ld hl,0
        ret
TIMER_NULL_RESPONSE:
        ld hl,3
        jr TIMER_REPORT_ERROR
TIMER_INVALID_ARGUMENT:
        ld hl,4
TIMER_REPORT_ERROR:
        push hl
        call _io_abort_timer
        pop bc
        ret
#endasm
#endif

static int report_error(int error_code)
{
    const char *message = "\r\nService I/O error ";
    int remaining_value = error_code, digit = 0;

    while (*message)
        putchar(*message++);
    while (remaining_value >= 100) {
        remaining_value -= 100;
        ++digit;
    }
    if (digit)
        putchar('0' + digit);
    digit = 0;
    while (remaining_value >= 10) {
        remaining_value -= 10;
        ++digit;
    }
    if (digit || error_code >= 100)
        putchar('0' + digit);
    putchar('0' + remaining_value);
    putchar('\r');
    putchar('\n');
    return -error_code;
}

static int fail_service(int error_code)
{
    outp(SERVICE_COMMAND_PORT, 1);
    return report_error(error_code);
}

/* Assembly needs a stable external symbol rather than dcc's static mangling. */
void io_abort_timer(int error_code)
{
    report_error(error_code);
    exit(1);
}

int svc_begin_request(int service_group, int request_length)
{
    if (request_length < 1 || request_length > SERVICE_MAX_PAYLOAD)
        return fail_service(3);
    if (inp(SERVICE_COMMAND_PORT) != 0)
        return fail_service(8);
    outp(SERVICE_COMMAND_PORT, service_group);
    if (inp(SERVICE_COMMAND_PORT) != 3)
        return fail_service(1);
    outp(SERVICE_REQUEST_PORT, request_length & 255);
    outp(SERVICE_REQUEST_PORT, request_length >> 8);
    return 0;
}

static inline void write_request_byte(int byte)
{
    outp(SERVICE_REQUEST_PORT, byte);
}

int svc_finish_response(void *response, int response_capacity)
{
    int status, error_code;
    unsigned response_length, remaining_length, copy_length, length_high_byte;

    if (response_capacity < 0 || response_capacity > SERVICE_MAX_PAYLOAD ||
        (response_capacity > 0 && !response))
        return fail_service(3);
    /* CP/M has no independent clock while the shared channel is occupied.
       This finite polling budget is not a calibrated seconds timeout. */
    status = io_wait_completion();
    if (status == 4)
        return fail_service(7);
    if (status == 1) {
        outp(SERVICE_COMMAND_PORT, 0);
        return 0;
    }
    if (status != 2 && status != 255)
        return fail_service(8);
    response_length = inp(SERVICE_RESPONSE_PORT) & 255;
    length_high_byte = inp(SERVICE_RESPONSE_PORT) & 255;
    response_length |= length_high_byte << 8;
    if (response_length > SERVICE_MAX_PAYLOAD)
        return fail_service(3);
    error_code = 8;
    remaining_length = response_length;
    if (status == 255) {
        if (remaining_length != 0) {
            error_code = inp(SERVICE_RESPONSE_PORT) & 255;
            --remaining_length;
        }
    } else {
        copy_length = response_length;
        if (copy_length > (unsigned)response_capacity)
            copy_length = response_capacity;
        remaining_length -= copy_length;
        io_read_response(response, copy_length);
    }
    io_drain_response(remaining_length);
    if (status == 255)
        return fail_service(error_code);
    if (response_length > (unsigned)response_capacity)
        return fail_service(19);
    return response_length;
}

int svc_call_service(int service_group, const void *request, int request_length,
                 void *response, int response_capacity)
{
    int result;

    if (!request || response_capacity < 0 || response_capacity > SERVICE_MAX_PAYLOAD ||
        (response_capacity > 0 && !response))
        return fail_service(3);
    result = svc_begin_request(service_group, request_length);
    if (result < 0)
        return result;
    io_write_request(request, request_length);
    return svc_finish_response(response, response_capacity);
}

int svc_operation_send(int service_group, int operation, const void *request,
                   int request_length, void *response, int response_capacity)
{
    int result;

    if (request_length < 0 || request_length >= SERVICE_MAX_PAYLOAD ||
        (request_length > 0 && !request) || response_capacity < 0 ||
        response_capacity > SERVICE_MAX_PAYLOAD || (response_capacity > 0 && !response))
        return fail_service(3);
    result = svc_begin_request(service_group, request_length + 1);
    if (result < 0)
        return result;
    write_request_byte(operation);
    io_write_request(request, request_length);
    return svc_finish_response(response, response_capacity);
}

int svc_get_text(int service_group, int operation, char *response, int response_capacity)
{
    int response_length;

    if (response_capacity < 1 || !response)
        return fail_service(3);
    response_length = svc_operation_send(service_group, operation, NULL, 0,
                                      response, response_capacity - 1);
    if (response_length < 0) {
        response[0] = 0;
        return response_length;
    }
    response[response_length] = 0;
    return response_length;
}

int svc_query_text(int service_group, int operation, int selector, char *response,
                int response_capacity)
{
    unsigned char request[2];
    int response_length;

    if (response_capacity < 1 || !response)
        return fail_service(3);
    request[0] = operation;
    request[1] = selector;
    response_length = svc_call_service(service_group, request, 2,
                                    response, response_capacity - 1);
    if (response_length < 0) {
        response[0] = 0;
        return response_length;
    }
    response[response_length] = 0;
    return response_length;
}

unsigned svc_random_u16(void)
{
    unsigned char request[1], response[2];
    unsigned random_value, high_byte;
    int result;

    request[0] = 0;
    result = svc_call_service(SERVICE_GROUP_UTILITY, request, 1, response, 2);
    if (result != 2) {
        if (result >= 0)
            fail_service(3);
        exit(1);
    }
    random_value = response[0];
    high_byte = response[1];
    random_value |= high_byte << 8;
    return random_value;
}
