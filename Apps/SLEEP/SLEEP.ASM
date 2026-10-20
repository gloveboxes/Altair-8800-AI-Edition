; ============================================================================
; SLEEP - Pause for a number of seconds, cancellable with ESC or Ctrl-C
; ============================================================================
; Usage:
;   SLEEP n        wait n seconds (n is decimal, 0..65535)
;
; The delay uses dedicated timer port 23, timer ID 3 (seconds).
; OUT 23,19 then the duration starts it; OUT 23,35 selects running status.
;
; Timer 3 retains its 0..255-second range despite the u32 wire duration.
; Longer waits are serviced in chunks of up to 255 seconds each.
;
; While waiting we poll the console with BDOS direct console I/O (function 6,
; E=0FFh) so a keypress is detected without blocking. ESC (1Bh) or Ctrl-C
; (03h) aborts the wait and returns to CP/M.
;
; Build with the macro assembler:
;   mac sleep        ->  SLEEP.HEX (plus .PRN/.SYM)
;   load sleep       ->  SLEEP.COM
;
; MAC constraints: symbols unique in first 6 chars; I/O ports in decimal.
; ============================================================================

BDOS    EQU     5               ; CP/M BDOS entry point
FPRSTR  EQU     9               ; BDOS func 9: print '$'-terminated string
FDCONIO EQU     6               ; BDOS func 6: direct console I/O
NOCHAR  EQU     0FFH            ; func 6 input request (E=0FFh)
TAILLN  EQU     0080H           ; command tail length byte
TAILCH  EQU     0081H           ; first character of the command tail
MAXCHK  EQU     255             ; largest timer-3 START duration
SPACE   EQU     ' '
ESCKEY  EQU     1BH             ; ESC
CTRLC   EQU     03H             ; Ctrl-C

; -- macro: print a '$'-terminated string at label STR ---------------------
PRINT   MACRO   STR
        LXI     D,STR
        MVI     C,FPRSTR
        CALL    BDOS
        ENDM

        ORG     0100H           ; CP/M transient program area

; -- locate the first argument in the command tail -------------------------
START:  LDA     TAILLN          ; B = number of bytes in the command tail
        MOV     B,A
        LXI     D,TAILCH        ; DE -> first character of the tail

SKIPSP: MOV     A,B             ; characters remaining?
        ORA     A
        JZ      NOARG           ; empty tail -> show usage
        LDAX    D
        CPI     SPACE
        JNZ     PARSE           ; non-space -> start of the number
        INX     D
        DCR     B
        JMP     SKIPSP

; -- parse a decimal number into HL ----------------------------------------
PARSE:  LXI     H,0             ; HL = running value
        MVI     C,0             ; C = "saw at least one digit" flag

PDIG:   MOV     A,B             ; characters remaining?
        ORA     A
        JZ      PDONE
        LDAX    D
        CPI     '0'             ; below '0' ?
        JC      PDONE
        CPI     3AH             ; ':' = '9'+1, at/above means not a digit
        JNC     PDONE
        SUI     '0'             ; A = digit value 0..9

        PUSH    PSW             ; preserve the digit
        PUSH    D               ; preserve the tail pointer
        MOV     D,H             ; HL = HL * 10 (DE used as scratch)
        MOV     E,L
        DAD     H               ;   x2
        DAD     H               ;   x4
        DAD     D               ;   x5
        DAD     H               ;   x10
        POP     D               ; restore the tail pointer
        POP     PSW             ; restore the digit
        ADD     L               ; HL = HL + digit
        MOV     L,A
        MOV     A,H
        ACI     0
        MOV     H,A

        MVI     C,1             ; remember a digit was seen
        INX     D
        DCR     B
        JMP     PDIG

PDONE:  MOV     A,C             ; any digits at all?
        ORA     A
        JZ      NOARG           ; none -> usage

; -- sleep loop: HL holds the seconds still to wait ------------------------
SLPLP:  MOV     A,H             ; remaining == 0 ?
        ORA     L
        JZ      FINISH          ; nothing left -> done

        MOV     A,H             ; pick this chunk's count
        ORA     A
        JNZ     BIGCHK          ; high byte set -> use the max chunk
        MOV     A,L             ; otherwise the low byte (1..255)
        JMP     HAVECH
BIGCHK: MVI     A,MAXCHK
HAVECH: MOV     B,A             ; B = chunk count (saved for subtraction)
        MVI     A,19
        OUT     23
        MOV     A,B
        OUT     23
        MVI     A,35
        OUT     23
        XRA     A
        STA     WTHI
        STA     WTLO
        STA     WTLO+1

; At most 2^24 status reads per chunk, not an infinite timer wait.
WAITLP: IN      23
        ORA     A
        JZ      CHKDON
        CPI     1
        JNZ     SVFAIL
        CALL    CKKEY           ; honour ESC / Ctrl-C while waiting
        PUSH    H
        LHLD    WTLO
        DCX     H
        SHLD    WTLO
        MOV     A,H
        ORA     L
        POP     H
        JNZ     WAITLP
        LDA     WTHI
        DCR     A
        STA     WTHI
        JZ      SVFAIL
        JMP     WAITLP

CHKDON: MOV     A,L             ; remaining -= chunk
        SUB     B
        MOV     L,A
        MOV     A,H
        SBI     0
        MOV     H,A
        JMP     SLPLP

FINISH: RET                     ; back to CP/M

; -- check the console for a cancel key (non-blocking) ---------------------
; Returns normally if no cancel key; never returns on ESC / Ctrl-C.
CKKEY:  PUSH    H               ; protect the seconds counter
        PUSH    B               ; protect the chunk count
        MVI     C,FDCONIO
        MVI     E,NOCHAR
        CALL    BDOS
        POP     B
        POP     H
        ORA     A
        RZ                      ; no key pending
        CPI     ESCKEY
        JZ      CANCEL
        CPI     CTRLC
        JZ      CANCEL
        RET                     ; some other key -> ignore, keep waiting

CANCEL: PRINT   CANMSG          ; report and abort via warm boot
        JMP     0               ; warm boot returns cleanly to the CCP

NOARG:  PRINT   USAGE
        RET

USAGE:  DB      'Usage: SLEEP n   (seconds; ESC or ^C cancels)',0DH,0AH,'$'
CANMSG: DB      0DH,0AH,'SLEEP cancelled',0DH,0AH,'$'
WTLO:   DW      0
WTHI:   DB      0

SVFAIL: IN      23
        XRA     A
        OUT     23
        LXI     D,SVMSG
        MVI     C,9
        CALL    5
        JMP     0
SVMSG:  DB      13,10,'Timer error/timeout; interface reset',13,10,'$'

        END
