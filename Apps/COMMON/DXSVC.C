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

/* Assembly needs an external symbol for the diagnostic setting. */
int io_diagnostics = 1;

int svc_error_output(int enabled)
{
    int previous = io_diagnostics;
    io_diagnostics = enabled != 0;
    return previous;
}

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
	ld a,(_io_diagnostics)
	or a
	jp z,ERROR_RETURN
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
ERROR_RETURN:
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

    if (!io_diagnostics)
        return -error_code;
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

/* Speaker options {flags, pitch, speed} and the last request ID. */
unsigned char io_sound[4] = {0, 100, 100, 0};

void snd_options(int flags, int pitch, int speed)
{
    io_sound[0] = flags;
    io_sound[1] = pitch;
    io_sound[2] = speed;
}

int snd_init(void)
{
    snd_options(0, 100, 100);
    inp(SOUND_PORT);
    outp(SOUND_PORT, 0);
    if (inp(SOUND_PORT) != SOUND_PROTOCOL_VERSION)
        return 0;
    snd_stop();
    while (snd_event())
        ;
    return 1;
}

/* Speaker packets: an IN restarts framing, then each byte is one OUT.
 * snd_fx walks a per-effect field layout: 1 = byte (remembered as a count),
 * 2 = big-endian word, 3 = pointer to count {word, word} pairs.
 * Layouts share suffixes, so the ten effects need 24 bytes.
 */
#if defined(_DCC_) && !defined(DXSVC_C_REFERENCE) && !defined(__INTELLISENSE__) && !defined(__clang__)
#asm
;@dcc.lto begin SND_NEXT_ID
SND_NEXT_ID:
	ld a,(_io_sound+3)
	inc a
	jr nz,SND_ID_READY
	inc a
SND_ID_READY:
	ld (_io_sound+3),a
	ret
;@dcc.lto end SND_NEXT_ID

;@dcc.lto begin _snd_fx
	public _snd_fx
_snd_fx:
	ld hl,5
	add hl,sp
	ld a,(hl)
	dec hl
	or a
	jr nz,SND_FX_UNKNOWN
	ld a,(hl)
	dec a
	cp 10
	jr nc,SND_FX_UNKNOWN
	add a,a
	ld e,a
	ld d,0
	ld hl,SND_LAYOUTS
	add hl,de
	ld e,(hl)
	inc hl
	ld d,(hl)
	in a,(21)
	ld a,16
	out (21),a
	call SND_NEXT_ID
	out (21),a
	ld hl,2
	add hl,sp
	ld a,(hl)
	out (21),a
	ld a,(_io_sound)
	out (21),a
	ld a,(_io_sound+1)
	out (21),a
	ld a,(_io_sound+2)
	out (21),a
	inc hl
	inc hl
	ld a,(hl)
	out (21),a
SND_FX_NEXT:
	inc hl
	inc hl
SND_FX_FIELD:
	ld a,(de)
	inc de
	dec a
	jr z,SND_FX_BYTE
	dec a
	jr z,SND_FX_WORD
	dec a
	jr nz,SND_FX_RETURN
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	ld a,c
	add a,a
	jr z,SND_FX_RETURN
	ld b,a
SND_FX_ARRAY:
	ld e,(hl)
	inc hl
	ld a,(hl)
	inc hl
	out (21),a
	ld a,e
	out (21),a
	djnz SND_FX_ARRAY
	jr SND_FX_RETURN
SND_FX_BYTE:
	ld a,(hl)
	ld c,a
	out (21),a
	jr SND_FX_NEXT
SND_FX_WORD:
	inc hl
	ld a,(hl)
	out (21),a
	dec hl
	ld a,(hl)
	out (21),a
	jr SND_FX_NEXT
SND_FX_UNKNOWN:
	ld hl,0
	ret
SND_FX_RETURN:
	ld a,(_io_sound+3)
	ld l,a
	ld h,0
	ret
SND_LAYOUTS:
	dw SND_L_TONE,SND_L_SWEEP,SND_L_SIREN,SND_L_SEQUENCE,SND_L_SEGMENTS
	dw SND_L_PULSES,SND_L_SIREN,SND_L_ARPEGGIO,SND_L_GATE,SND_L_PRESET
SND_L_PULSES:
	db 1,2,2,2,2,0
SND_L_SIREN:
	db 2
SND_L_GATE:
	db 2
SND_L_SWEEP:
	db 2
SND_L_TONE:
	db 2,2,0
SND_L_SEQUENCE:
	db 1,2,3
SND_L_SEGMENTS:
	db 1,2,2,3
SND_L_ARPEGGIO:
	db 2,2,2
SND_L_PRESET:
	db 1,0
;@dcc.lto end _snd_fx

;@dcc.lto begin SND_COMMAND
SND_COMMAND:
	in a,(21)
	ld a,b
	out (21),a
	call SND_NEXT_ID
	out (21),a
	ld l,a
	ld h,0
	ret
;@dcc.lto end SND_COMMAND

;@dcc.lto begin _snd_stop
	public _snd_stop
_snd_stop:
	ld b,17
	jp SND_COMMAND
;@dcc.lto end _snd_stop

;@dcc.lto begin _snd_clear
	public _snd_clear
_snd_clear:
	ld b,19
	jp SND_COMMAND
;@dcc.lto end _snd_clear

;@dcc.lto begin _snd_update
	public _snd_update
_snd_update:
	ld b,18
	call SND_COMMAND
	ld c,l
	ld hl,2
	add hl,sp
	ld b,4
SND_UPDATE_BYTE:
	ld a,(hl)
	out (21),a
	inc hl
	inc hl
	djnz SND_UPDATE_BYTE
	ld b,2
SND_UPDATE_WORD:
	inc hl
	ld a,(hl)
	out (21),a
	dec hl
	ld a,(hl)
	out (21),a
	inc hl
	inc hl
	djnz SND_UPDATE_WORD
	ld l,c
	ld h,0
	ret
;@dcc.lto end _snd_update

;@dcc.lto begin _snd_read
	public _snd_read
_snd_read:
	ld hl,2
	add hl,sp
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	in a,(21)
	xor a
	out (21),a
	ld b,19
SND_READ_BYTE:
	in a,(21)
	ld (hl),a
	inc hl
	djnz SND_READ_BYTE
	ret
;@dcc.lto end _snd_read

;@dcc.lto begin _snd_busy
	public _snd_busy
_snd_busy:
	in a,(21)
	xor a
	out (21),a
	in a,(21)
	in a,(21)
	in a,(21)
	ld l,a
	ld h,0
	ret
;@dcc.lto end _snd_busy

;@dcc.lto begin _snd_wait
	public _snd_wait
_snd_wait:
	call _snd_busy
	ld a,l
	or a
	jr nz,_snd_wait
	ret
;@dcc.lto end _snd_wait

;@dcc.lto begin _snd_event
	public _snd_event
_snd_event:
	in a,(21)
	xor a
	out (21),a
	ld b,11
SND_EVENT_SKIP:
	in a,(21)
	djnz SND_EVENT_SKIP
	in a,(21)
	ld d,a
	in a,(21)
	ld e,a
	in a,(21)
	ld l,a
	in a,(21)
	ld h,a
	or a
	jr z,SND_EVENT_NONE
	ld a,20
	out (21),a
	ld a,d
	out (21),a
	ld a,e
	out (21),a
	ret
SND_EVENT_NONE:
	ld l,a
	ret
;@dcc.lto end _snd_event
#endasm
#endif

#if !defined(_DCC_) || defined(DXSVC_C_REFERENCE) || defined(__INTELLISENSE__) || defined(__clang__)
#include <stdarg.h>

static int sound_command(int command)
{
    inp(SOUND_PORT);
    outp(SOUND_PORT, command);
    if (!++io_sound[3])
        ++io_sound[3];
    outp(SOUND_PORT, io_sound[3]);
    return io_sound[3];
}

static void sound_word(unsigned value)
{
    outp(SOUND_PORT, (value >> 8) & 255);
    outp(SOUND_PORT, value & 255);
}

int snd_fx(int priority, int effect, ...)
{
    static const unsigned char layouts[10][5] = {
        {2, 2}, {2, 2, 2}, {2, 2, 2, 2, 2}, {1, 2, 3}, {1, 2, 2, 3},
        {1, 2, 2, 2, 2}, {2, 2, 2, 2, 2}, {2, 2, 2, 1}, {2, 2, 2, 2}, {1}
    };
    const unsigned char *field;
    const unsigned short *pairs;
    unsigned count = 0, i;
    int id;
    va_list ap;

    if (effect < 1 || effect > 10)
        return 0;
    id = sound_command(16);
    outp(SOUND_PORT, priority & 255);
    outp(SOUND_PORT, io_sound[0]);
    outp(SOUND_PORT, io_sound[1]);
    outp(SOUND_PORT, io_sound[2]);
    outp(SOUND_PORT, effect);
    va_start(ap, effect);
    for (field = layouts[effect - 1]; field < layouts[effect - 1] + 5 && *field; ++field) {
        if (*field == 1) {
            count = va_arg(ap, int) & 255;
            outp(SOUND_PORT, count);
        } else if (*field == 2) {
            sound_word(va_arg(ap, unsigned));
        } else {
            pairs = va_arg(ap, const unsigned short *);
            for (i = 0; i < 2 * count; ++i)
                sound_word(pairs[i]);
        }
    }
    va_end(ap);
    return id;
}

int snd_stop(void)
{
    return sound_command(17);
}

int snd_clear(void)
{
    return sound_command(19);
}

int snd_update(int target, int mask, int pitch, int speed, unsigned depth_hz,
               unsigned period_ms)
{
    int id = sound_command(18);

    outp(SOUND_PORT, target & 255);
    outp(SOUND_PORT, mask & 255);
    outp(SOUND_PORT, pitch & 255);
    outp(SOUND_PORT, speed & 255);
    sound_word(depth_hz);
    sound_word(period_ms);
    return id;
}

void snd_read(unsigned char *status)
{
    int i;

    inp(SOUND_PORT);
    outp(SOUND_PORT, 0);
    for (i = 0; i < SND_STATUS_BYTES; ++i)
        status[i] = inp(SOUND_PORT);
}

int snd_busy(void)
{
    inp(SOUND_PORT);
    outp(SOUND_PORT, 0);
    inp(SOUND_PORT);
    inp(SOUND_PORT);
    return inp(SOUND_PORT) & 255;
}

void snd_wait(void)
{
    while (snd_busy())
        ;
}

int snd_event(void)
{
    unsigned char status[SND_S_EVENT_TYPE + 1];
    int i;

    inp(SOUND_PORT);
    outp(SOUND_PORT, 0);
    for (i = 0; i <= SND_S_EVENT_TYPE; ++i)
        status[i] = inp(SOUND_PORT);
    if (!status[SND_S_EVENT_TYPE])
        return 0;
    outp(SOUND_PORT, 20);
    outp(SOUND_PORT, status[SND_S_EVENT_SEQUENCE]);
    outp(SOUND_PORT, status[SND_S_EVENT_SEQUENCE + 1]);
    return (status[SND_S_EVENT_TYPE] << 8) | status[SND_S_EVENT_ID];
}
#endif
