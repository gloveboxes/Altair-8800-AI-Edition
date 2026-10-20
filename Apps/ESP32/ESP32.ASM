; ============================================================================
; ESP32 - ESP32 Stats CLI Tool for CP/M (MAC assembly port)
; ============================================================================
; Prints system/network statistics via grouped utility/clock services.
; Plain CLI output: no screen layout, colors, or cursor control.
;
; Build (CP/M):  mac esp32 / load esp32  ->  ESP32.COM
;
; MAC constraints: symbols unique in first 6 characters; ports in decimal.
; ============================================================================

BDOS    EQU     5               ; CP/M BDOS entry point
FCONO   EQU     2               ; console output, char in E
FPRST   EQU     9               ; print '$'-terminated string at DE

CR      EQU     0DH
LF      EQU     0AH

        ORG     0100H

START:  LXI     H,0             ; save CCP stack and use our own
        DAD     SP
        SHLD    OLDSP
        LXI     SP,STACK

        LXI     D,MHEAD
        CALL    PSTR
        LXI     D,MSUB
        CALL    PSTR
        CALL    CRLF
        LXI     D,MSYS
        CALL    PSTR

        MVI     A,0             ; hostname
        CALL    INFO
        CALL    RDSTR
        LXI     D,LHOST
        CALL    FIELD

        MVI     A,1             ; WiFi IP address
        CALL    INFO
        CALL    RDSTR
        LXI     D,LWIFI
        CALL    FIELD

        MVI     A,2             ; device ID
        CALL    INFO
        CALL    RDSTR
        LXI     D,LDEVID
        CALL    FIELD

        MVI     A,33            ; utilities VERSION operation
        LXI     H,VREQ
        LXI     B,1
        CALL    SVREQ
        CALL    RDSTR
        LXI     D,LEMU
        CALL    FIELD

        MVI     A,32            ; clock BOOT_SECONDS operation
        LXI     H,UREQ
        LXI     B,1
        CALL    SVREQ
        CALL    RDSTR
        CALL    CRLF
        LXI     D,MUPH
        CALL    PSTR
        LXI     D,LSECS
        CALL    FIELD

        CALL    DIV36           ; BUF / 3600 -> QBUF hours, REMV seconds
        CALL    DIV60           ; REMV / 60 -> MINV minutes
        LXI     D,LHM
        CALL    PSTR
        LXI     H,QBUF
        CALL    PNUL
        MVI     A,':'
        CALL    PCHR
        CALL    PR2D
        CALL    CRLF

EXIT:   LHLD    OLDSP
        SPHL
        RET

; -- ESP32 response helpers -------------------------------------------------
INFO:   STA     IREQ+1
        MVI     A,33
        LXI     H,IREQ
        LXI     B,2
        JMP     SVREQ

RDSTR:  CALL    SVTEXT          ; wire frame has already been fully drained
        LHLD    SVLEN
        MOV     A,H
        ORA     A
        JNZ     SVFAIL         ; do not silently truncate a long response
        MOV     B,L
        LXI     H,SVBUF
        LXI     D,BUF
RDS1:   MOV     A,B
        ORA     A
        JZ      RDS0
        MOV     A,M
        STAX    D
        INX     D
        INX     H
        DCR     B
        JMP     RDS1
RDS0:   XRA     A
        STAX    D              ; local text terminator, not a wire byte
        RET

FIELD:  CALL    PSTR            ; DE -> padded field label, value is BUF
        LXI     H,BUF
        CALL    PNUL
        JMP     CRLF

; -- uptime formatting ------------------------------------------------------
DIV36:  LXI     H,0             ; decimal long divide BUF by 3600
        SHLD    REMV
        XRA     A
        STA     QANY
        LXI     H,QBUF
        SHLD    QWR
        LXI     H,BUF
        SHLD    SRCPT
D361:   LHLD    SRCPT
        MOV     A,M
        ORA     A
        JZ      D365
        INX     H
        SHLD    SRCPT
        CPI     '0'
        JC      D361
        CPI     '9'+1
        JNC     D361
        SUI     '0'
        MOV     C,A
        LHLD    REMV
        CALL    MUL10
        MOV     A,L
        ADD     C
        MOV     L,A
        MOV     A,H
        ACI     0
        MOV     H,A
        MVI     B,'0'
D362:   MOV     A,L             ; subtract 3600 while possible
        SUI     010H
        MOV     E,A
        MOV     A,H
        SBI     0EH
        JC      D363
        MOV     H,A
        MOV     L,E
        INR     B
        JMP     D362
D363:   SHLD    REMV
        LDA     QANY
        ORA     A
        JNZ     D364
        MOV     A,B
        CPI     '0'
        JZ      D361
        MVI     A,1
        STA     QANY
D364:   LHLD    QWR
        MOV     M,B
        INX     H
        SHLD    QWR
        JMP     D361
D365:   LDA     QANY
        ORA     A
        JNZ     D366
        LHLD    QWR
        MVI     M,'0'
        INX     H
        SHLD    QWR
D366:   LHLD    QWR
        MVI     M,0
        RET

MUL10:  PUSH    D               ; HL = HL * 10
        MOV     D,H
        MOV     E,L
        DAD     H
        DAD     H
        DAD     H
        DAD     D
        DAD     D
        POP     D
        RET

DIV60:  LHLD    REMV            ; REMV / 60 -> MINV
        MVI     B,0
D601:   MOV     A,L
        SUI     60
        MOV     E,A
        MOV     A,H
        SBI     0
        JC      D602
        MOV     H,A
        MOV     L,E
        INR     B
        JMP     D601
D602:   MOV     A,B
        STA     MINV
        RET

PR2D:   LDA     MINV            ; print minutes as two decimal digits
        MVI     B,'0'
P2D1:   CPI     10
        JC      P2D2
        SUI     10
        INR     B
        JMP     P2D1
P2D2:   MOV     C,A
        MOV     A,B
        CALL    PCHR
        MOV     A,C
        ADI     '0'
        JMP     PCHR

; -- console output ---------------------------------------------------------
PNUL:   MOV     A,M             ; print NUL-terminated string at HL
        ORA     A
        RZ
        CALL    PCHR
        INX     H
        JMP     PNUL

PSTR:   PUSH    B               ; print '$'-terminated string at DE
        PUSH    H
        MVI     C,FPRST
        CALL    BDOS
        POP     H
        POP     B
        RET

PCHR:   PUSH    B               ; print char in A
        PUSH    D
        PUSH    H
        MOV     E,A
        MVI     C,FCONO
        CALL    BDOS
        POP     H
        POP     D
        POP     B
        RET

CRLF:   LXI     D,MCRLF
        JMP     PSTR

; -- messages ---------------------------------------------------------------
MHEAD:  DB      'ESP32 Stats - Altair 8800',CR,LF,'$'
MSUB:   DB      'Onboard system and network status',CR,LF,'$'
MSYS:   DB      'System',CR,LF,'$'
LHOST:  DB      '  Hostname        ','$'
LWIFI:  DB      '  WiFi IP         ','$'
LDEVID: DB      '  Device ID       ','$'
LEMU:   DB      '  Emulator        ','$'
MUPH:   DB      'Uptime',CR,LF,'$'
LSECS:  DB      '  Seconds         ','$'
LHM:    DB      '  Hours:mins      ','$'
MCRLF:  DB      CR,LF,'$'

; -- state ------------------------------------------------------------------
OLDSP:  DW      0
SRCPT:  DW      0
QWR:    DW      0
REMV:   DW      0
QANY:   DB      0
MINV:   DB      0
BUF:    DS      256
QBUF:   DS      12
        DS      128
STACK   EQU     $

IREQ:   DB      1,0
VREQ:   DB      3
UREQ:   DB      0

; Standalone bounded, counted service transport. Preserves BC/DE/HL.
SVREQ:  PUSH    B
        PUSH    D
        PUSH    H
        PUSH    PSW
        IN      2
        ORA     A
        JNZ     SVBAD
        POP     PSW
        OUT     2
        MOV     A,C
        OUT     3
        MOV     A,B
        OUT     3
SVSEND: MOV     A,B
        ORA     C
        JZ      SVPOLL
        MOV     A,M
        OUT     3
        INX     H
        DCX     B
        JMP     SVSEND
SVPOLL: MVI     B,32
        LXI     D,0
SVWAIT: IN      2
        CPI     3
        JZ      SVTICK
        CPI     4
        JNZ     SVSTAT
SVTICK: DCX     D
        MOV     A,D
        ORA     E
        JNZ     SVWAIT
        DCR     B
        JNZ     SVWAIT
        JMP     SVFAIL
SVSTAT: STA     SVTYP
        CPI     1
        JNZ     SVRESP
        XRA     A
        OUT     2
        LXI     H,0
        SHLD    SVLEN
        JMP     SVRET
SVRESP: CPI     2
        JZ      SVREAD
        CPI     255
        JNZ     SVFAIL
SVREAD: IN      4
        MOV     C,A
        IN      4
        MOV     B,A
        CPI     8
        JC      SVSIZE
        JNZ     SVFAIL
        MOV     A,C
        ORA     A
        JNZ     SVFAIL
SVSIZE: MOV     H,B
        MOV     L,C
        SHLD    SVLEN
        LXI     H,SVBUF
SVLOOP: MOV     A,B
        ORA     C
        JZ      SVDONE
        IN      4
        MOV     M,A
        INX     H
        DCX     B
        JMP     SVLOOP
SVDONE: MVI     M,0
        LDA     SVTYP
        CPI     255
        JZ      SVFAIL
        IN      2
        ORA     A
        JNZ     SVFAIL
SVRET:  POP     H
        POP     D
        POP     B
        RET
SVBAD:  CPI     255
        JZ      SVSTAT
SVFAIL: MVI     A,1
        OUT     2
        LXI     D,SVMSG
        MVI     C,9
        CALL    5
        JMP     0
SVTEXT: LDA     SVTYP
        CPI     2
        JNZ     SVFAIL
        RET
SVMSG:  DB      13,10,'Service error/timeout; transport reset',13,10,'$'
SVTYP:  DB      0
SVLEN:  DW      0
SVBUF:  DS      2049
        END