100 REM ====================================================
110 REM  WEBSOCKET THROUGHPUT / TERMINAL PERF TEST
120 REM  Streams N lines and times the run with clock group 32
130 REM  (seconds-since-boot), using counted service frames.
140 REM ====================================================
150 WIDTH 255
152 ES$ = CHR$(27)
154 DIM CL$(5)
156 FOR J = 0 TO 5 : CL$(J) = ES$ + "[1;3" + CHR$(49 + J) + "m" : NEXT J
158 RST$ = ES$ + "[0m"
160 N = 10000
170 PRINT "WEBSOCKET THROUGHPUT TEST - ";N;" LOOPS"
180 REM --- capture start time ---
190 G=32:Q$=CHR$(0):E=-1:GOSUB 9000:TS=VAL(R$)
200 REM --- the streaming workload ---
210 FOR I = 1 TO N
220 PRINT CL$(I - INT(I / 6) * 6);"The quick brown fox jumps over the lazy dog 012345678901234567890 # ";I;RST$
230 NEXT I
240 REM --- capture end time ---
250 G=32:Q$=CHR$(0):E=-1:GOSUB 9000:TE=VAL(R$)
260 GOSUB 8000
270 END
280 REM
8000 REM ====================================================
8010 REM  REPORT: compute and print elapsed time and rate
8020 REM ====================================================
8030 D = TE - TS
8040 PRINT
8050 PRINT "LOOPS          : ";N
8060 PRINT "ELAPSED SECONDS: ";D
8070 IF D > 0 THEN PRINT "LINES/SEC      : ";N / D
8080 IF D <= 0 THEN PRINT "LINES/SEC      : (too fast - increase N)"
8090 RETURN
8100 REM
9000 REM GROUP G, RAW PAYLOAD Q$, E=EXPECTED BYTES (-1 FOR TEXT)
9010 IF INP(2)<>0 THEN GOTO 9800
9020 OUT 2,G:OUT 3,LEN(Q$) MOD 256:OUT 3,INT(LEN(Q$)/256)
9030 FOR X=1 TO LEN(Q$):OUT 3,ASC(MID$(Q$,X,1)):NEXT X
9040 FOR P=1 TO 30000
9050 S=INP(2)
9060 IF S=1 OR S=2 OR S=255 THEN GOTO 9100
9070 IF S<>3 AND S<>4 THEN GOTO 9800
9080 NEXT P
9090 PRINT "Service timeout":GOTO 9800
9100 R$="":RL=0
9110 IF S=1 THEN OUT 2,0:IF E=0 THEN RETURN ELSE GOTO 9800
9120 RL=INP(4):RL=RL+256*INP(4)
9130 IF RL>2048 THEN GOTO 9800
9140 FOR X=1 TO RL
9150 V=INP(4)
9160 IF S<>255 AND RL<=255 THEN R$=R$+CHR$(V)
9170 NEXT X
9180 IF S=255 THEN PRINT "Service error":GOTO 9800
9190 IF E=0 OR RL>255 THEN GOTO 9800
9200 IF E>=0 AND RL<>E THEN GOTO 9800
9210 IF INP(2)<>0 THEN GOTO 9800
9220 RETURN
9800 OUT 2,1:PRINT "Service failed; transport reset":END
