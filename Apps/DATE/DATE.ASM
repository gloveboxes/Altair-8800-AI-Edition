; DATE - local DATE-format clock, grouped service ABI.
; Build: ASM DATE / LOAD DATE. Terminal and disk I/O are unchanged.
        ORG     100H
START:  MVI     A,32
        LXI     H,REQ
        LXI     B,1
        CALL    SVREQ
        CALL    SVTEXT
        LHLD    SVLEN
        MOV     B,H
        MOV     C,L
        LXI     H,SVBUF
PRLOOP: MOV     A,B
        ORA     C
        JZ      DONE
        MOV     E,M
        PUSH    B
        PUSH    H
        MVI     C,2
        CALL    5
        POP     H
        POP     B
        INX     H
        DCX     B
        JMP     PRLOOP
DONE:   LXI     D,CRLF
        MVI     C,9
        CALL    5
        RET
REQ:    DB      4
CRLF:   DB      13,10,'$'

; Standalone transport (ASM.COM has no source includes).
; A=group, BC=length, HL=raw payload. Preserves BC/DE/HL.
; Result SVTYP=1 COMPLETE, 2 response; SVLEN and SVBUF are counted.
; Bounded polling, full frame drain, RESET/error/warm boot on failure.
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
SVDONE: MVI     M,0             ; local convenience, never wire termination
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
        JZ      SVSTAT         ; drain pending error before RESET
SVFAIL: MVI     A,1
        OUT     2
        LXI     D,SVMSG
        MVI     C,9
        CALL    5
        JMP     0
SVNONE: LDA     SVTYP
        CPI     1
        JNZ     SVFAIL
        RET
SVTEXT: LDA     SVTYP
        CPI     2
        JNZ     SVFAIL
        RET
SVFOUR: CALL    SVTEXT
        PUSH    H
        LHLD    SVLEN
        MOV     A,H
        ORA     A
        JNZ     SVFAIL
        MOV     A,L
        CPI     4
        JNZ     SVFAIL
        POP     H
        RET
SVMSG:  DB      13,10,'Service error/timeout; transport reset',13,10,'$'
SVTYP:  DB      0
SVLEN:  DW      0
; Reserve the full protocol capacity in RAM, not in the COM file.
SVBUF   EQU     $
WORKND  EQU     SVBUF+2049
        END
