; ============================================================================
; DATE - Display the host machine's date and time, DATE-style
; ============================================================================
; Works on CP/M 2.2 and CP/M 3. The Altair emulator has no real-time clock,
; so the built-in DATE command (CP/M 3 only) shows a frozen, and for years
; >= 2000 corrupted, value. This DATE.COM instead asks the host for the
; current local date/time and prints it with a correct four-digit year.
; Because CP/M runs a transient .COM in preference to a built-in command,
; this DATE.COM also shadows the buggy CP/M 3 built-in DATE.
;
; Emulator I/O ports (see altair_local/PortDrivers/time_io.c,
; format_now_string):
;
;   OUT 31  - capture the host's local date/time and stage a printable,
;             NUL-terminated string in the port buffer, formatted as:
;                 "Sun 05/31/2026 17:13:00"   (%a %m/%d/%Y %H:%M:%S)
;   IN  200 - read the staged string back one byte at a time; a 0 byte
;             marks the end of the string.
;
; Build (CP/M):  asm date / load date  ->  DATE.COM
;
; ASM.COM constraints: symbols unique in first 6 chars; ports in decimal.
; ============================================================================

BDOS    EQU     5              ; CP/M BDOS entry point
FPRCHR  EQU     2              ; BDOS func 2: console output (char in E)
CLKCAP  EQU     31             ; OUT port: capture host clock into buffer
CLKRD   EQU     200            ; IN  port: read next buffer byte

        ORG     0100H          ; CP/M transient program area

START:  MVI     A,0            ; value is irrelevant; OUT triggers capture
        OUT     CLKCAP         ; host stages the printable date/time string

PRLOOP: IN      CLKRD          ; A = next character of the string
        ORA     A              ; zero byte?
        JZ      DONE           ; yes -> end of string
        MOV     E,A            ; character to print -> E
        MVI     C,FPRCHR       ; BDOS console output
        PUSH    PSW            ; preserve A across the BDOS call
        CALL    BDOS
        POP     PSW
        JMP     PRLOOP

DONE:   MVI     E,0DH          ; carriage return
        MVI     C,FPRCHR
        CALL    BDOS
        MVI     E,0AH          ; line feed
        MVI     C,FPRCHR
        CALL    BDOS
        RET                    ; return to CCP
        END
