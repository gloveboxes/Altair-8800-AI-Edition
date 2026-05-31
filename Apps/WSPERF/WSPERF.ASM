; ============================================================================
; WSPERF - WebSocket / 80-column terminal throughput test (8080 assembler)
; ============================================================================
; ASM port of WSPERF.C. Pushes rainbow text to the console as fast as the
; link allows, then reports lines/second using the host stopwatch (port 37).
;
; Press ESC at any time to stop early; the stats for the lines emitted so
; far are printed and the program exits 0 (returns to the CCP).
;
; Timing (see altair_local/PortDrivers/time_io.c):
;   OUT 37,0 -> start stopwatch 0
;   OUT 37,1 -> latch elapsed seconds as a 4-byte big-endian unsigned long
;   IN  200  -> read those 4 bytes back (MSB first)
; The line count, elapsed seconds and rate all fit in 16 bits here, so only
; the low 16 bits of the latched value are used.
;
; Output uses raw BIOS CONOUT so the VT100 color escapes and CR/LF go out as
; exact bytes (no tab/ctrl cooking). Header and labels use BDOS func 9.
;
; Command line:  WSPERF [-C]   ->  -C reruns the test forever until ESC.
;
; Build (CP/M):  asm wsperf / load wsperf  ->  WSPERF.COM
;
; ASM.COM constraints: symbols unique in first 6 chars; ports in decimal.
; ============================================================================

BDOS    EQU     5               ; CP/M BDOS entry point
FPRSTR  EQU     9               ; BDOS func 9: print '$'-terminated string
SWPORT  EQU     37              ; stopwatch 0 port
RDPORT  EQU     200             ; request/string read-back port
ESC     EQU     27              ; VT100 escape
COUNT   EQU     10000           ; lines to send (<= 65535)
NCOLOR  EQU     6               ; rainbow cycle length
WIDTH   EQU     60              ; printable phrase chars per line

        ORG     0100H           ; CP/M transient program area

START:  LXI     H,0             ; save the CCP's stack pointer
        DAD     SP
        SHLD    OLDSP
        LXI     SP,STACK        ; switch to our own stack

; --- resolve BIOS CONST / CONIN / CONOUT vectors from page zero ----------
        LHLD    1               ; HL -> WBOOT (BIOS base + 3)
        LXI     D,3
        DAD     D               ; HL -> CONST
        SHLD    CONSTA
        LXI     D,3
        DAD     D               ; HL -> CONIN
        SHLD    CONINA
        LXI     D,3
        DAD     D               ; HL -> CONOUT
        SHLD    CONOTA

; --- build the 60-char wrapped phrase + " # " into PHR60 -----------------
        LXI     H,PHR60
        LXI     D,PHRASE
        MVI     B,WIDTH
PB1:    LDAX    D
        ORA     A               ; end of PHRASE?
        JNZ     PB2
        LXI     D,PHRASE        ; wrap back to the start
        LDAX    D
PB2:    MOV     M,A
        INX     H
        INX     D
        DCR     B
        JNZ     PB1
        MVI     M,' '
        INX     H
        MVI     M,'#'
        INX     H
        MVI     M,' '
        INX     H
        MVI     M,0             ; NUL terminator

; --- scan the CP/M command tail for -C (continuous mode) -----------------
        XRA     A
        STA     CONTIN
        LDA     0080H           ; command tail length
        MOV     B,A
        ORA     A
        JZ      ARGDON
        LXI     H,0081H         ; first command-tail char
ARGLP:  MOV     A,M
        CPI     'C'             ; CCP upper-cases the tail
        JZ      ARGSET
        CPI     'c'
        JZ      ARGSET
        INX     H
        DCR     B
        JNZ     ARGLP
        JMP     ARGDON
ARGSET: MVI     A,1
        STA     CONTIN
ARGDON:

; --- one pass of the test (re-entered here in continuous mode) -----------
RUNTST: XRA     A
        STA     ESCFLG          ; assume ESC not pressed this pass

; --- header: "WS THROUGHPUT TEST - <COUNT> LINES" -------------------------
        LXI     D,HDR1
        CALL    PSTR
        LXI     H,COUNT
        CALL    PRDEC
        LXI     D,HDR2
        CALL    PSTR

; --- start the stopwatch and init counters ------------------------------
        XRA     A
        OUT     SWPORT          ; OUT 37,0 -> start stopwatch 0
        LXI     H,0
        SHLD    LINES           ; completed lines = 0
        XRA     A
        STA     CIDX            ; color index = 0

; --- the hot loop -------------------------------------------------------
LOOP:   LHLD    LINES           ; done when LINES >= COUNT
        LXI     D,COUNT
        MOV     A,H
        CMP     D
        JC      LBODY           ; HL < DE -> keep going
        JNZ     LDONE           ; HL > DE -> done
        MOV     A,L
        CMP     E
        JC      LBODY           ; HL < DE -> keep going
        JMP     LDONE           ; HL >= DE -> done

LBODY:  CALL    COLOUT          ; ESC [ 1 ; 3<color> m
        LXI     H,PHR60         ; phrase + " # "
        CALL    COUTS
        LHLD    LINES           ; counter = LINES + 1
        INX     H
        CALL    PRDEC
        LXI     H,SUFFIX        ; ESC [ 0 m CR LF
        CALL    COUTS
        LHLD    LINES           ; LINES = LINES + 1
        INX     H
        SHLD    LINES
        LDA     CIDX            ; advance color index (mod NCOLOR)
        INR     A
        CPI     NCOLOR
        JC      CSET
        XRA     A
CSET:   STA     CIDX
        CALL    CKESC           ; ESC pressed?
        ORA     A
        JNZ     LESC            ; yes -> stop early
        JMP     LOOP
LESC:   MVI     A,1
        STA     ESCFLG          ; remember ESC so we don't rerun
        JMP     LDONE

; --- latch elapsed seconds, keep the low 16 bits ------------------------
LDONE:  MVI     A,1
        OUT     SWPORT          ; OUT 37,1 -> latch elapsed seconds
        IN      RDPORT          ; byte 0 (MSB) - ignore
        IN      RDPORT          ; byte 1       - ignore
        IN      RDPORT          ; byte 2
        MOV     H,A
        IN      RDPORT          ; byte 3 (LSB)
        MOV     L,A
        SHLD    ELAPS           ; HL = elapsed seconds (low 16 bits)

; --- report -------------------------------------------------------------
        CALL    CRLF
        LXI     D,MLINES
        CALL    PSTR
        LHLD    LINES
        CALL    PRDEC
        CALL    CRLF
        LXI     D,MELAP
        CALL    PSTR
        LHLD    ELAPS
        CALL    PRDEC
        CALL    CRLF
        LHLD    ELAPS           ; elapsed == 0 ?
        MOV     A,H
        ORA     L
        JNZ     HRATE
        LXI     D,MFAST         ; "(too fast - raise COUNT)"
        CALL    PSTR
        CALL    CRLF
        JMP     RPTEND
HRATE:  LXI     D,MRATE
        CALL    PSTR
        LHLD    LINES           ; rate = LINES / ELAPS
        MOV     B,H
        MOV     C,L
        LHLD    ELAPS
        XCHG                    ; DE = ELAPS
        CALL    UDIVR           ; HL = quotient
        CALL    PRDEC
        CALL    CRLF

; --- continuous mode: rerun unless ESC ended the pass --------------------
RPTEND: LDA     CONTIN
        ORA     A
        JZ      FIN             ; not continuous -> done
        LDA     ESCFLG
        ORA     A
        JNZ     FIN             ; ESC pressed -> done
        CALL    CRLF            ; blank line between runs
        JMP     RUNTST

FIN:    LHLD    OLDSP           ; restore CCP stack and exit 0
        SPHL
        RET

; ============================================================================
; UDIVR - unsigned divide BC / DE -> HL quotient (repeated subtraction)
;         Only called once, so simplicity beats speed. DE must be nonzero.
; ============================================================================
UDIVR:  LXI     H,0             ; quotient = 0
UDR1:   MOV     A,C             ; BC = BC - DE
        SUB     E
        MOV     C,A
        MOV     A,B
        SBB     D
        MOV     B,A
        JC      UDRD            ; borrow -> BC < DE, done
        INX     H               ; quotient++
        JMP     UDR1
UDRD:   RET

; ============================================================================
; DIV10 - unsigned HL / 10 -> HL quotient, A remainder (0-9)
; ============================================================================
DIV10:  PUSH    B
        PUSH    D
        LXI     D,0             ; DE = running remainder
        MVI     B,16            ; 16 bits
DIV10L: DAD     H               ; HL <<= 1, CY = old bit 15
        MOV     A,E
        RAL
        MOV     E,A
        MOV     A,D
        RAL
        MOV     D,A             ; DE = (DE << 1) | CY
        MOV     A,E             ; DE - 10
        SUI     10
        MOV     C,A
        MOV     A,D
        SBI     0
        JC      DIV10N          ; DE < 10 -> leave remainder, quotient bit 0
        MOV     D,A             ; accept subtraction
        MOV     E,C
        INR     L               ; quotient bit 0 = 1 (L bit0 was 0 after DAD H)
DIV10N: DCR     B
        JNZ     DIV10L
        MOV     A,E             ; remainder (0-9)
        POP     D
        POP     B
        RET

; ============================================================================
; PRDEC - print unsigned HL as decimal (no leading zeros) via CONOUT
; ============================================================================
PRDEC:  PUSH    H
        PUSH    D
        PUSH    B
        LXI     D,NMEND         ; DE -> just past digit buffer
PRD1:   CALL    DIV10           ; HL = quotient, A = remainder
        ADI     '0'
        DCX     D
        STAX    D               ; store digit at --DE
        MOV     A,H
        ORA     L
        JNZ     PRD1            ; until quotient == 0
PRD2:   LDAX    D               ; print digits DE .. NMEND-1
        MOV     C,A
        CALL    COUT
        INX     D
        MOV     A,E             ; DE == NMEND ?
        LXI     H,NMEND
        CMP     L
        JNZ     PRD2
        MOV     A,D
        CMP     H
        JNZ     PRD2
        POP     B
        POP     D
        POP     H
        RET

; ============================================================================
; COLOUT - emit "ESC [ 1 ; 3 <digit> m" for the current color (CIDX 0-5)
; ============================================================================
COLOUT: PUSH    H
        MVI     C,ESC
        CALL    COUT
        MVI     C,'['
        CALL    COUT
        MVI     C,'1'
        CALL    COUT
        MVI     C,';'
        CALL    COUT
        MVI     C,'3'
        CALL    COUT
        LDA     CIDX
        ADI     '1'             ; colors 0..5 -> '1'..'6' (31..36)
        MOV     C,A
        CALL    COUT
        MVI     C,'m'
        CALL    COUT
        POP     H
        RET

; ============================================================================
; COUTS - output the NUL-terminated string at HL via CONOUT
; ============================================================================
COUTS:  MOV     A,M
        ORA     A
        RZ
        MOV     C,A
        PUSH    H
        CALL    COUT
        POP     H
        INX     H
        JMP     COUTS

; ============================================================================
; CKESC - return A=1 if an ESC keypress is waiting, A=0 otherwise.
;         A non-ESC key is consumed and ignored.
; ============================================================================
CKESC:  CALL    CONST
        ORA     A
        JZ      CKNO            ; no character ready
        CALL    CONIN
        CPI     ESC
        JZ      CKYES
CKNO:   XRA     A
        RET
CKYES:  MVI     A,1
        RET

; ============================================================================
; CRLF - carriage return + line feed via CONOUT
; ============================================================================
CRLF:   MVI     C,13
        CALL    COUT
        MVI     C,10
        CALL    COUT
        RET

; ============================================================================
; PSTR - print the '$'-terminated string at DE via BDOS func 9
; ============================================================================
PSTR:   PUSH    H
        PUSH    D
        PUSH    B
        MVI     C,FPRSTR
        CALL    BDOS
        POP     B
        POP     D
        POP     H
        RET

; --- raw BIOS console primitives (HL clobbered by the LHLD/PCHL) ---------
COUT:   LHLD    CONOTA          ; C = char out
        PCHL
CONST:  LHLD    CONSTA          ; A = 0 (none) / nonzero (ready)
        PCHL
CONIN:  LHLD    CONINA          ; A = char in
        PCHL

; ============================================================================
; Strings
; ============================================================================
HDR1:   DB      'WS THROUGHPUT TEST - $'
HDR2:   DB      ' LINES',13,10,'$'
MLINES: DB      'LINES          : $'
MELAP:  DB      'ELAPSED SECONDS: $'
MRATE:  DB      'LINES/SEC      : $'
MFAST:  DB      'LINES/SEC      : (too fast - raise COUNT)$'
SUFFIX: DB      ESC,'[','0','m',13,10,0
PHRASE: DB      'The quick brown fox jumps over the lazy dog ',0

; ============================================================================
; Scratch RAM
; ============================================================================
OLDSP:  DS      2               ; saved CCP stack pointer
CONSTA: DS      2               ; BIOS CONST vector
CONINA: DS      2               ; BIOS CONIN vector
CONOTA: DS      2               ; BIOS CONOUT vector
LINES:  DS      2               ; completed line count
ELAPS:  DS      2               ; elapsed seconds (low 16 bits)
CIDX:   DS      1               ; current color index (0..NCOLOR-1)
CONTIN: DS      1               ; nonzero if -C (continuous) given
ESCFLG: DS      1               ; nonzero if ESC ended the current pass
NUMBUF: DS      6               ; PRDEC digit buffer (max 5 digits)
NMEND   EQU     $               ; one past NUMBUF
PHR60:  DS      64              ; wrapped phrase + " # " + NUL
        DS      64              ; stack space
STACK   EQU     $               ; top of our stack
        END
