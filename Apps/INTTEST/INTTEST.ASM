; INTTEST - exercise the emulator's periodic Z80 interrupt source.
;
; OUT 52,n selects n maskable interrupts per second; zero disables it.
; The device supplies FFH during interrupt acknowledge. This program uses
; Z80 interrupt mode 1, so accepted interrupts enter through 0038H.
;
; A CP/M .COM file starts at 0100H and cannot contain code at 0038H. Install
; a JP stub there while this program runs, then restore CP/M's original bytes.
; The ISR must not call BDOS because it can interrupt BDOS itself.

BDOS    EQU     5
FPRINT  EQU     9
INTPRT  EQU     52
INTVEC  EQU     0038H
TARGET  EQU     50

        ORG     0100H

START:  DI
        LDA     INTVEC
        STA     OLDVEC
        LDA     INTVEC+1
        STA     OLDVEC+1
        LDA     INTVEC+2
        STA     OLDVEC+2

        MVI     A,0C3H         ; JP ISR
        STA     INTVEC
        LXI     H,ISR
        SHLD    INTVEC+1

        LXI     D,MESSAGE
        MVI     C,FPRINT
        CALL    BDOS

        MVI     A,10           ; 10 interrupts per second
        OUT     INTPRT
        DB      0EDH,056H      ; IM 1 (MAC has no Z80 mnemonic)
        EI

WAITT:  HLT                     ; timer interrupt resumes at the next instruction
        LHLD    TICKS
        LXI     D,TARGET
        MOV     A,H
        CMP     D
        JNZ     WAITT
        MOV     A,L
        CMP     E
        JNZ     WAITT

CLEAN:  DI
        XRA     A
        OUT     INTPRT         ; stop source and discard pending ticks
        LDA     OLDVEC
        STA     INTVEC
        LDA     OLDVEC+1
        STA     INTVEC+1
        LDA     OLDVEC+2
        STA     INTVEC+2

        LXI     D,COUNTM
        MVI     C,FPRINT
        CALL    BDOS
        LHLD    TICKS
        CALL    PHWORD
        LXI     D,CRLF
        MVI     C,FPRINT
        CALL    BDOS
        RET

ISR:    PUSH    PSW
        PUSH    B
        PUSH    D
        PUSH    H
        LHLD    TICKS
        INX     H
        SHLD    TICKS
        POP     H
        POP     D
        POP     B
        POP     PSW
        EI
        DB      0EDH,04DH      ; RETI

PHWORD: PUSH    H
        MOV     A,H
        CALL    PHBYTE
        POP     H
        MOV     A,L
PHBYTE: PUSH    PSW
        RRC
        RRC
        RRC
        RRC
        ANI     0FH
        CALL    PHNIB
        POP     PSW
        ANI     0FH
PHNIB:  ADI     '0'
        CPI     '9'+1
        JC      PHOUT
        ADI     'A'-'9'-1
PHOUT:  MOV     E,A
        MVI     C,2
        JMP     BDOS

TICKS:  DW      0
OLDVEC: DS      3
MESSAGE: DB     'Counting 50 interrupts at 10 Hz...$'
COUNTM: DB      0DH,0AH,'Interrupt count (hex): $'
CRLF:   DB      0DH,0AH,'$'
        END