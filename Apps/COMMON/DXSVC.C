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
int io_print_error(int error_code);
int io_fail_service(int error_code);

/* Native tests supply mocked primitives; dcc always uses this assembly.
 * Caller cleans arguments; HL returns int; IX/IY and interrupt state survive.
 * AF/BC/DE/HL are scratch. Ports use immediate IN/OUT, not ED block I/O.
 * Tab indentation keeps inline text within dcc's 8K pending-assembly buffer.
 */
#if defined(_DCC_) && !defined(DXSVC_C_REFERENCE) && !defined(__INTELLISENSE__) && !defined(__clang__)
#asm
;@dcc.lto begin _io_print_error
	public _io_print_error
	extrn __pchr
_io_print_error:
	; Local and wire error codes are bytes; the result is a signed int.
	push ix
	ld ix,0
	add ix,sp
	ld hl,SERVICE_ERROR_MESSAGE
ERROR_MESSAGE_LOOP:
	ld a,(hl)
	or a
	jr z,ERROR_HUNDREDS
	inc hl
	push hl
	call ERROR_PUT_CHAR
	pop hl
	jr ERROR_MESSAGE_LOOP
ERROR_HUNDREDS:
	ld e,(ix+4)
	ld d,0
	ld a,e
ERROR_HUNDREDS_LOOP:
	cp 100
	jr c,ERROR_HUNDREDS_DONE
	sub 100
	inc d
	jr ERROR_HUNDREDS_LOOP
ERROR_HUNDREDS_DONE:
	ld e,a
	ld a,d
	or a
	jr z,ERROR_TENS
	add a,48
	call ERROR_PUT_CHAR
ERROR_TENS:
	ld d,0
	ld a,e
ERROR_TENS_LOOP:
	cp 10
	jr c,ERROR_TENS_DONE
	sub 10
	inc d
	jr ERROR_TENS_LOOP
ERROR_TENS_DONE:
	ld e,a
	ld a,d
	or a
	jr nz,ERROR_PRINT_TENS
	ld a,(ix+4)
	cp 100
	jr c,ERROR_ONES
	ld a,d
ERROR_PRINT_TENS:
	add a,48
	call ERROR_PUT_CHAR
ERROR_ONES:
	ld a,e
	add a,48
	call ERROR_PUT_CHAR
	ld a,13
	call ERROR_PUT_CHAR
	ld a,10
	call ERROR_PUT_CHAR
	ld e,(ix+4)
	ld d,(ix+5)
	ld hl,0
	or a
	sbc hl,de
	pop ix
	ret
ERROR_PUT_CHAR:
	push de
	ld l,a
	ld h,0
	push hl
	call __pchr
	pop bc
	pop de
	ret
SERVICE_ERROR_MESSAGE:
	db 13,10,'Service I/O error ',0
;@dcc.lto end _io_print_error

;@dcc.lto begin _io_fail_service
	public _io_fail_service
_io_fail_service:
	ld a,1
	out (2),a
	jp _io_print_error
SERVICE_FAIL_RETURN:
	ld sp,ix
	push hl
	call _io_fail_service
	pop bc
	pop ix
	ret
;@dcc.lto end _io_fail_service

;@dcc.lto begin _svc_begin_request
	public _svc_begin_request
_svc_begin_request:
	push ix
	ld ix,0
	add ix,sp
	ld l,(ix+6)
	ld h,(ix+7)
	ld a,h
	or l
	jr z,BEGIN_INVALID
	ld a,h
	cp 8
	jr c,BEGIN_READY
	jr nz,BEGIN_INVALID
	ld a,l
	or a
	jr nz,BEGIN_INVALID
BEGIN_READY:
	in a,(2)
	or a
	jr nz,BEGIN_BUSY
	ld a,(ix+4)
	out (2),a
	in a,(2)
	cp 3
	jr nz,BEGIN_BAD_STATE
	ld a,(ix+6)
	out (3),a
	ld a,(ix+7)
	out (3),a
	ld hl,0
	pop ix
	ret
BEGIN_INVALID:
	ld hl,3
	jp SERVICE_FAIL_RETURN
BEGIN_BUSY:
	ld hl,8
	jp SERVICE_FAIL_RETURN
BEGIN_BAD_STATE:
	ld hl,1
	jp SERVICE_FAIL_RETURN
;@dcc.lto end _svc_begin_request

;@dcc.lto begin _svc_finish_response
	public _svc_finish_response
_svc_finish_response:
	push ix
	ld ix,0
	add ix,sp
	; Four private bytes hold status, error code and the reply length.
	ld hl,-4
	add hl,sp
	ld sp,hl
	ld l,(ix+6)
	ld h,(ix+7)
	ld a,h
	cp 8
	jr c,FINISH_CHECK_BUFFER
	jp nz,FINISH_INVALID
	ld a,l
	or a
	jp nz,FINISH_INVALID
FINISH_CHECK_BUFFER:
	ld a,h
	or l
	jr z,FINISH_WAIT
	ld a,(ix+4)
	or (ix+5)
	jp z,FINISH_INVALID
FINISH_WAIT:
	call _io_wait_completion
	ld a,l
	cp 4
	jp z,FINISH_TIMEOUT
	cp 1
	jp z,FINISH_COMPLETE
	cp 2
	jr z,FINISH_READ_LENGTH
	cp 255
	jp nz,FINISH_BAD_STATE
FINISH_READ_LENGTH:
	ld (ix-1),a
	in a,(4)
	ld e,a
	ld (ix-4),a
	in a,(4)
	ld d,a
	ld (ix-3),a
	cp 8
	jr c,FINISH_PAYLOAD
	jp nz,FINISH_INVALID
	ld a,e
	or a
	jp nz,FINISH_INVALID
FINISH_PAYLOAD:
	ld (ix-2),8
	ld a,(ix-1)
	cp 255
	jr nz,FINISH_COPY
	ld a,d
	or e
	jr z,FINISH_DRAIN
	in a,(4)
	ld (ix-2),a
	dec de
	jr FINISH_DRAIN
FINISH_COPY:
	ld l,(ix+6)
	ld h,(ix+7)
	or a
	sbc hl,de
	jr nc,FINISH_COPY_ALL
	ld c,(ix+6)
	ld b,(ix+7)
	ex de,hl
	or a
	sbc hl,bc
	jr FINISH_READ
FINISH_COPY_ALL:
	ld c,e
	ld b,d
	ld hl,0
FINISH_READ:
	push hl
	push bc
	ld l,(ix+4)
	ld h,(ix+5)
	push hl
	call _io_read_response
	pop bc
	pop bc
	pop de
FINISH_DRAIN:
	push de
	call _io_drain_response
	pop bc
	ld a,(ix-1)
	cp 255
	jr z,FINISH_SERVICE_ERROR
	ld e,(ix-4)
	ld d,(ix-3)
	ld l,(ix+6)
	ld h,(ix+7)
	or a
	sbc hl,de
	ld hl,19
	jp c,SERVICE_FAIL_RETURN
	ex de,hl
	jr FINISH_RETURN
FINISH_SERVICE_ERROR:
	ld l,(ix-2)
	ld h,0
	jp SERVICE_FAIL_RETURN
FINISH_COMPLETE:
	xor a
	out (2),a
	ld hl,0
FINISH_RETURN:
	ld sp,ix
	pop ix
	ret
FINISH_INVALID:
	ld hl,3
	jp SERVICE_FAIL_RETURN
FINISH_TIMEOUT:
	ld hl,7
	jp SERVICE_FAIL_RETURN
FINISH_BAD_STATE:
	ld hl,8
	jp SERVICE_FAIL_RETURN
;@dcc.lto end _svc_finish_response
#endasm
#endif

/* Assembly needs a stable external symbol rather than dcc's static mangling. */
void io_abort_timer(int error_code)
{
    io_print_error(error_code);
    exit(1);
}

#if defined(_DCC_) && !defined(DXSVC_C_REFERENCE) && !defined(__INTELLISENSE__) && !defined(__clang__)
#asm
;@dcc.lto begin _io_wait_completion
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
;@dcc.lto end _io_wait_completion

;@dcc.lto begin _io_write_request
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
;@dcc.lto end _io_write_request

;@dcc.lto begin _io_read_response
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
;@dcc.lto end _io_read_response

;@dcc.lto begin _io_drain_response
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
;@dcc.lto end _io_drain_response

;@dcc.lto begin _svc_start_timer
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
;@dcc.lto end _svc_start_timer

;@dcc.lto begin _svc_timer_active
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
;@dcc.lto end _svc_timer_active

;@dcc.lto begin _svc_access_stopwatch
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
;@dcc.lto end _svc_access_stopwatch

;@dcc.lto begin TIMER_INVALID_ARGUMENT
TIMER_INVALID_ARGUMENT:
	ld hl,4
TIMER_REPORT_ERROR:
	push hl
	call _io_abort_timer
	pop bc
	ret
;@dcc.lto end TIMER_INVALID_ARGUMENT
#endasm
#endif

#if !defined(_DCC_) || defined(DXSVC_C_REFERENCE) || defined(__INTELLISENSE__) || defined(__clang__)
int io_print_error(int error_code)
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

int io_fail_service(int error_code)
{
    outp(SERVICE_COMMAND_PORT, 1);
    return io_print_error(error_code);
}
#endif

#if !defined(_DCC_) || defined(DXSVC_C_REFERENCE) || defined(__INTELLISENSE__) || defined(__clang__)
int svc_begin_request(int service_group, int request_length)
{
    if (request_length < 1 || request_length > SERVICE_MAX_PAYLOAD)
        return io_fail_service(3);
    if (inp(SERVICE_COMMAND_PORT) != 0)
        return io_fail_service(8);
    outp(SERVICE_COMMAND_PORT, service_group);
    if (inp(SERVICE_COMMAND_PORT) != 3)
        return io_fail_service(1);
    outp(SERVICE_REQUEST_PORT, request_length & 255);
    outp(SERVICE_REQUEST_PORT, request_length >> 8);
    return 0;
}
#endif

static inline void write_request_byte(int byte)
{
    outp(SERVICE_REQUEST_PORT, byte);
}

#if !defined(_DCC_) || defined(DXSVC_C_REFERENCE) || defined(__INTELLISENSE__) || defined(__clang__)
int svc_finish_response(void *response, int response_capacity)
{
    int status, error_code;
    unsigned response_length, remaining_length, copy_length, length_high_byte;

    if (response_capacity < 0 || response_capacity > SERVICE_MAX_PAYLOAD ||
        (response_capacity > 0 && !response))
        return io_fail_service(3);
    /* CP/M has no independent clock while the shared channel is occupied.
       This finite polling budget is not a calibrated seconds timeout. */
    status = io_wait_completion();
    if (status == 4)
        return io_fail_service(7);
    if (status == 1) {
        outp(SERVICE_COMMAND_PORT, 0);
        return 0;
    }
    if (status != 2 && status != 255)
        return io_fail_service(8);
    response_length = inp(SERVICE_RESPONSE_PORT) & 255;
    length_high_byte = inp(SERVICE_RESPONSE_PORT) & 255;
    response_length |= length_high_byte << 8;
    if (response_length > SERVICE_MAX_PAYLOAD)
        return io_fail_service(3);
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
        return io_fail_service(error_code);
    if (response_length > (unsigned)response_capacity)
        return io_fail_service(19);
    return response_length;
}
#endif

int svc_call_service(int service_group, const void *request, int request_length,
                 void *response, int response_capacity)
{
    int result;

    if (!request || response_capacity < 0 || response_capacity > SERVICE_MAX_PAYLOAD ||
        (response_capacity > 0 && !response))
        return io_fail_service(3);
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
        return io_fail_service(3);
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
        return io_fail_service(3);
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
        return io_fail_service(3);
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
            io_fail_service(3);
        exit(1);
    }
    random_value = response[0];
    high_byte = response[1];
    random_value |= high_byte << 8;
    return random_value;
}
