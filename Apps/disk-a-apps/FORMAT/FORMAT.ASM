; FORMAT - recovered CP/M 2.2 disk initializer, BIOS-call compatibility fix.
; Reconstructed from the supplied FORMAT.COM; original author is unidentified.
; Intel 8080 instructions, compatible with CP/M MAC and the Altair 8080.
;
; FULL writes E5 to every track INCLUDING system tracks, then reads back.
; TEST reads every sector. DISK changes drive; STOP returns to CP/M.
; PARTIAL was a no-op in the original and remains a no-op here.
; Geometry and sector translation come from the selected BIOS DPH/DPB.
;
; Fixes: pass complete 16-bit track/sector values (B=0); pass C=1 to
; BIOS WRITE for synchronous writes; pass E=0 when selecting a disk.
; Named scratch buffers relocate together and do not overlap.
; Original command syntax/messages are retained; commands must be uppercase.
; Build on CP/M: MAC FORMAT, then LOAD FORMAT.
; Host build: python3 build-format.py
;
        ORG 0100H


; Locate BIOS entries relative to the warm-boot vector at 0001H.
START:
        LHLD  0001H
L0103:
        LXI   D,0003H
L0106:
        DAD   D
L0107:
        SHLD  L0137
L010A:
        DAD   D
L010B:
        SHLD  L013A
L010E:
        DAD   D
L010F:
        SHLD  L013D
L0112:
        DAD   D
L0113:
        DAD   D
L0114:
        DAD   D
L0115:
        DAD   D
L0116:
        DAD   D
L0117:
        SHLD  L0140
L011A:
        DAD   D
L011B:
        SHLD  L0143
L011E:
        DAD   D
L011F:
        SHLD  L0146
L0122:
        DAD   D
L0123:
        SHLD  L0149
L0126:
        DAD   D
L0127:
        SHLD  L014C
L012A:
        DAD   D
L012B:
        SHLD  L014F
L012E:
        DAD   D
L012F:
        DAD   D
L0130:
        SHLD  L0152
L0133:
        JMP   MAIN
L0136:
        JMP   0000H
L0139:
        JMP   0000H
L013C:
        JMP   0000H

; Self-modified BIOS jump stubs.
BSEL:
        JMP   0000H
BTRACK:
        JMP   0000H
BSECT:
        JMP   0000H
BDMA:
        JMP   0000H
BREAD:
        JMP   0000H
BWRITE:
        JMP   0000H
BXLATE:
        JMP   0000H
L0154:
        DB 00AH,00DH,00AH,00DH,02AH,02AH,02AH,020H,044H,049H,053H,04BH,045H,054H,054H,045H ; ....*** DISKETTE
        DB 020H,049H,04EH,049H,054H,049H,041H,04CH,049H,05AH,045H,052H,020H,02AH,02AH,02AH ;  INITIALIZER ***
        DB 00AH,00DH,024H ; ..$
L0177:
        DB 00AH,00DH,044H,049H,053H,04BH,020H,044H,052H,049H,056H,045H,020H,028H,041H,02DH ; ..DISK DRIVE (A-
        DB 050H,029H,03FH,020H,024H ; P)? $
L018C:
        DB 00AH,00DH,00AH,00DH,043H,04FH,04DH,04DH,041H,04EH,044H,03AH,020H,024H ; ....COMMAND: $
L019A:
        DB 00AH,00DH,056H,041H,04CH,049H,044H,020H,043H,04FH,04DH,04DH,041H,04EH,044H,053H ; ..VALID COMMANDS
        DB 020H,041H,052H,045H,03AH,020H,044H,049H,053H,04BH,02CH,046H,055H,04CH,04CH,02CH ;  ARE: DISK,FULL,
        DB 050H,041H,052H,054H,049H,041H,04CH,02CH,054H,045H,053H,054H,02CH,053H,054H,04FH ; PARTIAL,TEST,STO
        DB 050H,00AH,00DH,024H ; P..$
L01CE:
        DB 00AH,00DH,057H,041H,052H,04EH,049H,04EH,047H,03AH,020H,054H,048H,049H,053H,020H ; ..WARNING: THIS 
        DB 043H,04FH,04DH,04DH,041H,04EH,044H,020H,044H,045H,053H,054H,052H,04FH,059H,053H ; COMMAND DESTROYS
        DB 020H,041H,04CH,04CH,020H,044H,041H,054H,041H,020H,04FH,04EH,020H,054H,048H,045H ;  ALL DATA ON THE
        DB 020H,044H,049H,053H,04BH,00AH,00DH,054H,059H,050H,045H,020H,043H,020H,054H,04FH ;  DISK..TYPE C TO
        DB 020H,043H,04FH,04EH,054H,049H,04EH,055H,045H,02CH,020H,041H,020H,054H,04FH,020H ;  CONTINUE, A TO 
        DB 041H,042H,04FH,052H,054H,020H,057H,049H,054H,048H,04FH,055H,054H,020H,04CH,04FH ; ABORT WITHOUT LO
        DB 053H,053H,02EH,00AH,00AH,00DH,024H ; SS....$
L0235:
        DB 00DH,00AH,00AH,02AH,02AH,02AH,020H,043H,048H,045H,043H,04BH,049H,04EH,047H,020H ; ...*** CHECKING 
        DB 046H,04FH,052H,020H,042H,041H,044H,020H,053H,045H,043H,054H,04FH,052H,053H,020H ; FOR BAD SECTORS 
        DB 02AH,02AH,02AH,00DH,00AH,00AH,024H ; ***...$
L025C:
        DB 00DH,00AH,042H,041H,044H,020H,053H,045H,043H,054H,04FH,052H,020H,046H,04FH,055H ; ..BAD SECTOR FOU
        DB 04EH,044H,020H,041H,054H,020H,054H,052H,041H,043H,04BH,020H,024H ; ND AT TRACK $
L0279:
        DB 00DH,00AH,00AH,054H,045H,053H,054H,020H,052H,055H,04EH,020H,043H,04FH,04DH,050H ; ...TEST RUN COMP
        DB 04CH,045H,054H,045H,02EH,00DH,00AH,049H,046H,020H,041H,04EH,059H,020H,045H,052H ; LETE...IF ANY ER
        DB 052H,04FH,052H,053H,020H,057H,045H,052H,045H,020H,046H,04FH,055H,04EH,044H,02CH ; RORS WERE FOUND,
        DB 020H,059H,04FH,055H,020H,053H,048H,04FH,055H,04CH,044H,020H,043H,04FH,050H,059H ;  YOU SHOULD COPY
        DB 00DH,00AH,041H,04CH,04CH,020H,046H,049H,04CH,045H,053H,020H,046H,052H,04FH,04DH ; ..ALL FILES FROM
        DB 020H,054H,048H,045H,020H,044H,049H,053H,04BH,045H,054H,054H,045H,020H,041H,04EH ;  THE DISKETTE AN
        DB 044H,020H,046H,04FH,052H,04DH,041H,054H,020H,049H,054H,02EH,00DH,00AH,00AH,024H ; D FORMAT IT....$
L02E9:
        DB 050H,052H,04FH,043H,045H,053H,053H,049H,04EH,047H,020H,054H,052H,041H,043H,04BH ; PROCESSING TRACK
        DB 023H,020H,024H ; # $
L02FC:
        DB 00AH,00DH,045H,052H,052H,04FH,052H,020H,04FH,043H,043H,055H,052H,052H,045H,044H ; ..ERROR OCCURRED
        DB 020H,04FH,04EH,020H,054H,052H,041H,043H,04BH,020H,024H ;  ON TRACK $
L0317:
        DB 020H,053H,045H,043H,054H,04FH,052H,020H,024H ;  SECTOR $
L0320:
        DB 00AH,00DH,042H,041H,044H,020H,053H,045H,043H,054H,04FH,052H,020H,02DH,020H,053H ; ..BAD SECTOR - S
        DB 055H,047H,047H,045H,053H,054H,020H,046H,04FH,052H,04DH,041H,054H,054H,049H,04EH ; UGGEST FORMATTIN
        DB 047H,020H,04FH,052H,00AH,00DH,044H,049H,053H,043H,041H,052H,044H,049H,04EH,047H ; G OR..DISCARDING
        DB 020H,054H,048H,049H,053H,020H,044H,049H,053H,04BH,045H,054H,054H,045H,02EH,02EH ;  THIS DISKETTE..
        DB 02EH,00AH,00DH,024H ; ...$
L0364:
        DB 00AH,00DH,049H,04EH,049H,054H,020H,044H,04FH,04EH,045H,020H,02DH,020H,04EH,04FH ; ..INIT DONE - NO
        DB 057H,020H,056H,045H,052H,049H,046H,059H,049H,04EH,047H,020H,044H,049H,053H,04BH ; W VERIFYING DISK
        DB 045H,054H,054H,045H,00DH,00AH,024H ; ETTE..$
L038B:
        DB 00DH,00AH,04EH,04FH,020H,045H,052H,052H,04FH,052H,053H,020H,046H,04FH,055H,04EH ; ..NO ERRORS FOUN
        DB 044H,020H,04FH,04EH,020H,054H,048H,049H,053H,020H,044H,049H,053H,04BH,045H,054H ; D ON THIS DISKET
        DB 054H,045H,00DH,00AH,024H ; TE..$
L03B0:
        DB 00DH,00AH,00DH,00AH,053H,045H,043H,054H,04FH,052H,053H,020H,050H,045H,052H,020H ; ....SECTORS PER 
        DB 054H,052H,041H,043H,04BH,03AH,020H,024H ; TRACK: $
L03C8:
        DB 00DH,00AH,053H,045H,043H,054H,04FH,052H,053H,020H,050H,045H,052H,020H,042H,04CH ; ..SECTORS PER BL
        DB 04FH,043H,04BH,03AH,020H,024H ; OCK: $
L03DE:
        DB 00DH,00AH,042H,04CH,04FH,043H,04BH,053H,020H,050H,045H,052H,020H,044H,049H,053H ; ..BLOCKS PER DIS
        DB 04BH,03AH,020H,020H,020H,024H ; K:   $
L03F4:
        DB 00DH,00AH,052H,045H,053H,045H,052H,056H,045H,044H,020H,054H,052H,041H,043H,04BH ; ..RESERVED TRACK
        DB 053H,03AH,020H,020H,020H,024H ; S:   $
L040A:
        DB 00DH,00AH,054H,052H,041H,043H,04BH,053H,020H,050H,045H,052H,020H,044H,049H,053H ; ..TRACKS PER DIS
        DB 04BH,03AH,020H,020H,020H,024H ; K:   $
L0420:
        DB 00DH,024H ; .$
L0422:
        DB 00DH,00AH,024H ; ..$
L0425:
        DB 00AH,000H ; ..
L0427:
        DB 020H,020H,020H,020H,020H,020H,020H,020H,020H,020H,020H ;            
L0432:
        DB 000H ; .
L0433:
        DB 000H ; .
L0434:
        DB 000H,000H ; ..
L0436:
        DB 000H ; .
L0437:
        DB 000H ; .
L0438:
        DB 000H ; .
L0439:
        DB 000H ; .
L043A:
        DB 000H ; .
L043B:
        DB 000H,000H ; ..
L043D:
        DB 000H ; .
L043E:
        MOV   C,M
L043F:
        MOV   A,C
L0440:
        CPI   24H
L0442:
        RZ
L0443:
        CALL  L013C
L0446:
        INX   H
L0447:
        JMP   L043E
L044A:
        MOV   A,M
L044B:
        STAX  D
L044C:
        INX   H
L044D:
        INX   D
L044E:
        DCX   B
L044F:
        MOV   A,B
L0450:
        ORA   A
L0451:
        JNZ   L044A
L0454:
        MOV   A,C
L0455:
        ORA   A
L0456:
        JNZ   L044A
L0459:
        RET
L045A:
        MOV   M,C
L045B:
        INX   H
L045C:
        DCR   B
L045D:
        JNZ   L045A
L0460:
        RET
L0461:
        LDAX  D
L0462:
        CMP   M
L0463:
        RNZ
L0464:
        INX   H
L0465:
        INX   D
L0466:
        DCR   C
L0467:
        JNZ   L0461
L046A:
        RET
MAIN:
        LXI   H,L0154
L046E:
        CALL  L043E

; Select a drive and report its BIOS geometry.
L0471:
        LXI   H,L0177
L0474:
        CALL  L043E
L0477:
        LXI   D,L0425
L047A:
        MVI   C,0AH
L047C:
        CALL  0005H
L047F:
        LDA   L0427
L0482:
        ANI   0FH
L0484:
        DCR   A
L0485:
        STA   DRIVE
L0488:
        LDA   DRIVE
L048B:
        MOV   C,A
L048C:
        MVI E,0           ; first select / login
        CALL  BSEL
L048F:
        MOV   A,H
L0490:
        ORA   A
L0491:
        JNZ   L049C
L0494:
        MOV   A,L
L0495:
        ORA   A
L0496:
        JNZ   L049C
L0499:
        JMP   L0471
L049C:
        SHLD  L0434
L049F:
        MOV   A,M
L04A0:
        STA   L0432
L04A3:
        INX   H
L04A4:
        MOV   A,M
L04A5:
        STA   L0433
L04A8:
        INX   H
L04A9:
        LXI   D,0008H
L04AC:
        DAD   D
L04AD:
        MOV   A,M
L04AE:
        STA   L0436
L04B1:
        INX   H
L04B2:
        MOV   A,M
L04B3:
        STA   L0437
L04B6:
        LXI   H,L03B0
L04B9:
        CALL  L043E
L04BC:
        LHLD  L0436
L04BF:
        XCHG
L04C0:
        LDAX  D
L04C1:
        STA   L0438
L04C4:
        MOV   L,A
L04C5:
        MVI   H,00H
L04C7:
        CALL  L06EF
L04CA:
        LXI   H,L03C8
L04CD:
        CALL  L043E
L04D0:
        LHLD  L0436
L04D3:
        INX   H
L04D4:
        INX   H
L04D5:
        INX   H
L04D6:
        PUSH  H
L04D7:
        XCHG
L04D8:
        LDAX  D
L04D9:
        INR   A
L04DA:
        STA   L043A
L04DD:
        MOV   L,A
L04DE:
        MVI   H,00H
L04E0:
        CALL  L06EF
L04E3:
        LXI   H,L03DE
L04E6:
        CALL  L043E
L04E9:
        POP   H
L04EA:
        INX   H
L04EB:
        INX   H
L04EC:
        MOV   E,M
L04ED:
        INX   H
L04EE:
        MOV   D,M
L04EF:
        PUSH  H
L04F0:
        XCHG
L04F1:
        SHLD  L043B
L04F4:
        CALL  L06EF
L04F7:
        LXI   H,L03F4
L04FA:
        CALL  L043E
L04FD:
        POP   H
L04FE:
        INX   H
L04FF:
        LXI   D,0006H
L0502:
        DAD   D
L0503:
        MOV   A,M
L0504:
        STA   L043D
L0507:
        MOV   L,A
L0508:
        MVI   H,00H
L050A:
        CALL  L06EF
L050D:
        LXI   H,L040A
L0510:
        CALL  L043E
L0513:
        LHLD  L043B
L0516:
        XCHG
L0517:
        LHLD  L043A
L051A:
        MVI   H,00H
L051C:
        CALL  L0716
L051F:
        LDA   L0438
L0522:
        MOV   E,A
L0523:
        MVI   D,00H
L0525:
        CALL  L0730
L0528:
        LDA   L043D
L052B:
        MOV   E,A
L052C:
        MVI   D,00H
L052E:
        DAD   D
L052F:
        INX   H
L0530:
        MOV   A,L
L0531:
        STA   L0439
L0534:
        CALL  L06EF

; Command dispatcher (first uppercase letter).
CMD:
        LXI   H,L018C
L053A:
        CALL  L043E
L053D:
        LXI   D,L0425
L0540:
        MVI   C,0AH
L0542:
        CALL  0005H
L0545:
        LDA   L0427
L0548:
        CPI   44H
L054A:
        JZ    L0471
L054D:
        CPI   46H
L054F:
        JZ    FULL
L0552:
        CPI   50H
L0554:
        JZ    PART
L0557:
        CPI   54H
L0559:
        JZ    TEST
L055C:
        CPI   53H
L055E:
        JZ    STOP
L0561:
        LXI   H,L019A
L0564:
        CALL  L043E
L0567:
        JMP   CMD

; FULL: confirm, fill all sectors with E5, then verify.
FULL:
        LXI   H,L01CE
L056D:
        CALL  L043E
L0570:
        CALL  L0139
L0573:
        CPI   43H
L0575:
        JNZ   CMD
L0578:
        MVI   A,00H
L057A:
        STA   TRACK
L057D:
        INR   A
L057E:
        STA   SECTOR
L0581:
        MVI   C,0E5H
L0583:
        LXI   H,WRTBUF
L0586:
        MVI   B,80H
L0588:
        CALL  L045A
L058B:
        CALL  L0637

; Write loop: every sector is committed before advancing.
L058E:
        LDA   TRACK
L0591:
        MOV   C,A
L0592:
        MVI B,0           ; BIOS track/sector takes BC, not just C
        CALL  BTRACK
L0595:
        CALL  XLATE
L0598:
        MOV   C,A
L0599:
        MVI B,0           ; BIOS track/sector takes BC, not just C
        CALL  BSECT
L059C:
        LXI   B,WRTBUF
L059F:
        CALL  BDMA
L05A2:
        MVI C,1           ; CP/M WRITE type: synchronous directory write
        CALL  BWRITE
L05A5:
        ORA   A
L05A6:
        JNZ   L0655
L05A9:
        CALL  L0607
L05AC:
        CPI   0FFH
L05AE:
        JNZ   L058E
L05B1:
        LXI   H,L0364
L05B4:
        CALL  L043E
L05B7:
        MVI   A,00H
L05B9:
        STA   TRACK
L05BC:
        INR   A
L05BD:
        STA   SECTOR
L05C0:
        MVI   C,0E5H
L05C2:
        LXI   H,WRTBUF
L05C5:
        MVI   B,80H
L05C7:
        CALL  L045A
L05CA:
        CALL  L0637

; Read/compare loop.
L05CD:
        LDA   TRACK
L05D0:
        MOV   C,A
L05D1:
        MVI B,0           ; BIOS track/sector takes BC, not just C
        CALL  BTRACK
L05D4:
        CALL  XLATE
L05D7:
        MOV   C,A
L05D8:
        MVI B,0           ; BIOS track/sector takes BC, not just C
        CALL  BSECT
L05DB:
        LXI   B,RDBUF
L05DE:
        CALL  BDMA
L05E1:
        CALL  BREAD
L05E4:
        ORA   A
L05E5:
        JNZ   L0655
L05E8:
        LXI   H,WRTBUF
L05EB:
        LXI   D,RDBUF
L05EE:
        MVI   C,80H
L05F0:
        CALL  L0461
L05F3:
        JNZ   L0655
L05F6:
        CALL  L0607
L05F9:
        CPI   0FFH
L05FB:
        JNZ   L05CD
L05FE:
        LXI   H,L038B
L0601:
        CALL  L043E
L0604:
        JMP   CMD

; Advance sector (1-based display), then track.
L0607:
        PUSH  B
L0608:
        LDA   L0438
L060B:
        MOV   C,A
L060C:
        INR   C
L060D:
        LDA   L0439
L0610:
        MOV   B,A
L0611:
        LDA   SECTOR
L0614:
        INR   A
L0615:
        CMP   C
L0616:
        JZ    L061E
L0619:
        STA   SECTOR
L061C:
        POP   B
L061D:
        RET
L061E:
        MVI   A,01H
L0620:
        STA   SECTOR
L0623:
        LDA   TRACK
L0626:
        INR   A
L0627:
        CMP   B
L0628:
        JZ    L0633
L062B:
        STA   TRACK
L062E:
        CALL  L0637
L0631:
        POP   B
L0632:
        RET
L0633:
        MVI   A,0FFH
L0635:
        POP   B
L0636:
        RET

; Print progress while preserving registers.
L0637:
        PUSH  H
L0638:
        PUSH  D
L0639:
        PUSH  B
L063A:
        PUSH  PSW
L063B:
        LXI   H,L02E9
L063E:
        CALL  L043E
L0641:
        LDA   TRACK
L0644:
        MVI   H,00H
L0646:
        MOV   L,A
L0647:
        CALL  L06EF
L064A:
        LXI   H,L0420
L064D:
        CALL  L043E
L0650:
        POP   PSW
L0651:
        POP   B
L0652:
        POP   D
L0653:
        POP   H
L0654:
        RET

; Report an I/O or comparison error.
L0655:
        LXI   H,L02FC
L0658:
        CALL  L043E
L065B:
        MVI   H,00H
L065D:
        LDA   TRACK
L0660:
        MOV   L,A
L0661:
        CALL  L06EF
L0664:
        LXI   H,L0317
L0667:
        CALL  L043E
L066A:
        MVI   H,00H
L066C:
        LDA   SECTOR
L066F:
        MOV   L,A
L0670:
        CALL  L06EF
L0673:
        LXI   H,L0320
L0676:
        CALL  L043E
L0679:
        JMP   CMD

; Original PARTIAL command: unimplemented, returns to prompt.
PART:
        JMP   CMD

; TEST: read-only sector scan.
TEST:
        LXI   H,L0235
L0682:
        CALL  L043E
L0685:
        MVI   A,00H
L0687:
        STA   TRACK
L068A:
        INR   A
L068B:
        STA   SECTOR
L068E:
        MVI   C,0E5H
L0690:
        LXI   H,WRTBUF
L0693:
        MVI   B,80H
L0695:
        CALL  L045A
L0698:
        CALL  L0637
L069B:
        LDA   TRACK
L069E:
        MOV   C,A
L069F:
        MVI B,0           ; BIOS track/sector takes BC, not just C
        CALL  BTRACK
L06A2:
        CALL  XLATE
L06A5:
        MOV   C,A
L06A6:
        MVI B,0           ; BIOS track/sector takes BC, not just C
        CALL  BSECT
L06A9:
        LXI   B,RDBUF
L06AC:
        CALL  BDMA
L06AF:
        CALL  BREAD
L06B2:
        ORA   A
L06B3:
        CNZ   L06C7
L06B6:
        CALL  L0607
L06B9:
        CPI   0FFH
L06BB:
        JNZ   L069B
L06BE:
        LXI   H,L0279
L06C1:
        CALL  L043E
L06C4:
        JMP   CMD
L06C7:
        LXI   H,L025C
L06CA:
        CALL  L043E
L06CD:
        MVI   H,00H
L06CF:
        LDA   TRACK
L06D2:
        MOV   L,A
L06D3:
        CALL  L06EF
L06D6:
        LXI   H,L0317
L06D9:
        CALL  L043E
L06DC:
        MVI   H,00H
L06DE:
        LDA   SECTOR
L06E1:
        MOV   L,A
L06E2:
        CALL  L06EF
L06E5:
        LXI   H,L0422
L06E8:
        CALL  L043E
L06EB:
        RET
STOP:
        JMP   0000H

; Print unsigned decimal number.
L06EF:
        PUSH  D
L06F0:
        PUSH  B
L06F1:
        PUSH  PSW
L06F2:
        CALL  L06F9
L06F5:
        POP   PSW
L06F6:
        POP   B
L06F7:
        POP   D
L06F8:
        RET
L06F9:
        LXI   B,0FFF6H
L06FC:
        MOV   D,B
L06FD:
        MOV   E,B
L06FE:
        DAD   B
L06FF:
        INX   D
L0700:
        JC    L06FE
L0703:
        PUSH  H
L0704:
        XCHG
L0705:
        MOV   A,H
L0706:
        ORA   L
L0707:
        CNZ   L06FC
L070A:
        MVI   A,3AH
L070C:
        POP   B
L070D:
        ADD   C
L070E:
        MOV   B,A
L070F:
        PUSH  B
L0710:
        MOV   C,B
L0711:
        CALL  L013C
L0714:
        POP   B
L0715:
        RET

; Recovered arithmetic helpers used to compute track count.
L0716:
        MOV   C,L
L0717:
        MOV   B,H
L0718:
        LXI   H,0000H
L071B:
        MVI   A,0FH
L071D:
        PUSH  PSW
L071E:
        ORA   D
L071F:
        JP    L0723
L0722:
        DAD   B
L0723:
        DAD   H
L0724:
        XCHG
L0725:
        DAD   H
L0726:
        XCHG
L0727:
        POP   PSW
L0728:
        DCR   A
L0729:
        JNZ   L071D
L072C:
        ORA   D
L072D:
        RP
L072E:
        DAD   B
L072F:
        RET
L0730:
        MOV   A,H
L0731:
        STA   L07AF
L0734:
        XRA   D
L0735:
        STA   L07AE
L0738:
        MOV   A,D
L0739:
        ORA   A
L073A:
        JP    L0743
L073D:
        SUB   A
L073E:
        SUB   L
L073F:
        MOV   L,A
L0740:
        SBB   A
L0741:
        SUB   H
L0742:
        MOV   D,A
L0743:
        MOV   A,H
L0744:
        ORA   A
L0745:
        JP    L074E
L0748:
        SUB   A
L0749:
        SUB   E
L074A:
        MOV   E,A
L074B:
        SBB   A
L074C:
        SUB   D
L074D:
        MOV   H,A
L074E:
        CALL  L076D
L0751:
        RC
L0752:
        LDA   L07AE
L0755:
        ORA   A
L0756:
        JP    L0761
L0759:
        MVI   A,00H
L075B:
        SUB   L
L075C:
        MOV   L,A
L075D:
        MVI   A,00H
L075F:
        SBB   H
L0760:
        MOV   H,A
L0761:
        LDA   L07AF
L0764:
        ORA   A
L0765:
        RP
L0766:
        SUB   A
L0767:
        SUB   E
L0768:
        MOV   E,A
L0769:
        SBB   A
L076A:
        SUB   D
L076B:
        MOV   D,A
L076C:
        RET
L076D:
        MOV   A,E
L076E:
        ORA   D
L076F:
        JNZ   L0779
L0772:
        LXI   H,0000H
L0775:
        MOV   D,H
L0776:
        MOV   E,L
L0777:
        STC
L0778:
        RET
L0779:
        MOV   C,L
L077A:
        MOV   B,H
L077B:
        LXI   H,0000H
L077E:
        MVI   A,10H
L0780:
        ORA   A
L0781:
        STA   L07B0
L0784:
        MOV   A,C
L0785:
        RAL
L0786:
        MOV   C,A
L0787:
        MOV   A,B
L0788:
        RAL
L0789:
        MOV   B,A
L078A:
        MOV   A,L
L078B:
        RAL
L078C:
        MOV   L,A
L078D:
        MOV   A,H
L078E:
        RAL
L078F:
        MOV   H,A
L0790:
        PUSH  H
L0791:
        MOV   A,L
L0792:
        SUB   E
L0793:
        MOV   L,A
L0794:
        MOV   A,H
L0795:
        SBB   D
L0796:
        MOV   H,A
L0797:
        CMC
L0798:
        JC    L079C
L079B:
        XTHL
L079C:
        INX   SP
L079D:
        INX   SP
L079E:
        LDA   L07B0
L07A1:
        DCR   A
L07A2:
        JNZ   L0781
L07A5:
        XCHG
L07A6:
        MOV   A,C
L07A7:
        RAL
L07A8:
        MOV   L,A
L07A9:
        MOV   A,B
L07AA:
        RAL
L07AB:
        MOV   H,A
L07AC:
        ORA   A
L07AD:
        RET
L07AE:
        LXI   B,4BCDH

; Translate a 0-based logical sector using the selected DPH table.
XLATE:
        PUSH  H
L07B2:
        PUSH  D
L07B3:
        PUSH  B
L07B4:
        LDA   SECTOR
L07B7:
        DCR   A
L07B8:
        MOV   C,A
L07B9:
        MVI   B,00H
L07BB:
        LHLD  L0432
L07BE:
        XCHG
L07BF:
        LXI   H,0000H
L07C2:
        CALL  BXLATE
L07C5:
        MOV   A,L
L07C6:
        POP   B
L07C7:
        POP   D
L07C8:
        POP   H
L07C9:
        RET

; BIOS DMA buffers and per-operation state (not hardcoded addresses).
WRTBUF:
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
RDBUF:
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
        DB 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
DRIVE:  DB 0
TRACK:  DB 0
SECTOR: DB 0
L0137 EQU L0136+1
L013A EQU L0139+1
L013D EQU L013C+1
L0140 EQU BSEL+1
L0143 EQU BTRACK+1
L0146 EQU BSECT+1
L0149 EQU BDMA+1
L014C EQU BREAD+1
L014F EQU BWRITE+1
L0152 EQU BXLATE+1
L07AF EQU L07AE+1
L07B0 EQU L07AE+2
        END START
