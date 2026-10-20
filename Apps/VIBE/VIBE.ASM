; ============================================================================
; VIBE - Intel 8080 Assembly Demo Program for Altair 8800 / CP/M
; ============================================================================
; This program displays a counter message with a configurable delay between
; iterations. Originally coded with OpenAI Codex assistance.
;
; Features:
;   - Counts from 1 to 60,000 with 50ms delay between iterations
;   - ESC key interrupts and exits the program
;   - Demonstrates decimal number printing (up to 5 digits)
;   - Uses hardware timer for accurate delays
;
; CP/M / ASM.COM Constraints:
;   - Labels/symbols must be unique within first 6 characters (ASM truncates)
;   - BDOS entry point is always at 0005H
;   - Port constants use decimal (ASM.COM handles hex with leading zero oddly)
;   - BDOS calls can corrupt all registers except as documented
;
; Hardware Requirements:
;   - Intel 8080 CPU
;   - CP/M operating system
;   - Dedicated timer interface on port 23 (decimal), countdown timer 2
; ============================================================================

        ORG     0100H          ; Standard CP/M program load address (TPA start)

; ============================================================================
; CONSTANTS AND EQUATES
; ============================================================================
BDOS    EQU     0005H          ; CP/M BDOS entry point (system call interface)
TMRIO   EQU     23             ; Dedicated countdown/stopwatch port (decimal)
TMRSET  EQU     12H            ; Start millisecond countdown timer 2
TMRGET  EQU     22H            ; Select timer 2 running status

; ============================================================================
; PROGRAM INITIALIZATION
; ============================================================================
START:                          ; Program entry point (CP/M transfers control here)
        IN      TMRIO           ; Abort any incomplete timer parameter write
        XRA     A
        OUT     TMRIO           ; Reset timer interface without changing deadlines
        LXI     H,60000         ; Load HL with 60000 (total iterations to perform)
        SHLD    LOPCNT          ; Store in LOPCNT (remaining loop counter)
        LXI     H,1             ; Load HL with 1 (starting loop number)
        SHLD    LOPNUM          ; Store in LOPNUM (current iteration display number)

; ============================================================================
; MAIN PROGRAM LOOP
; ============================================================================
; This loop executes once per iteration, printing the message with counter,
; waiting for the specified delay, then incrementing counters. Exits when
; the remaining count reaches zero or ESC is pressed.
; ============================================================================
MAINLP:                         ; Main loop entry point
        ; Check if we've completed all iterations
        LHLD    LOPCNT          ; Load remaining loop count into HL
        MOV     A,H             ; Copy H to A for testing
        ORA     L               ; OR with L to test if HL is zero
        JZ      DONE            ; If zero, exit program

        ; Print the message string
        LXI     D,MSGTXT        ; Load DE with address of message text
        CALL    PRSTR           ; Print string via BDOS function 9

        ; Print the current loop number
        LHLD    LOPNUM          ; Load current loop number into HL
        CALL    PRDEC           ; Print HL as decimal number

        ; Print trailing characters (period, CR, LF)
        MVI     A,'.'           ; Load A with period character
        CALL    PRCHR           ; Print it
        MVI     A,0DH           ; Load A with carriage return (CR)
        CALL    PRCHR           ; Print it
        MVI     A,0AH           ; Load A with line feed (LF)
        CALL    PRCHR           ; Print it

        ; Wait for delay period (with ESC check)
        CALL    WAIT5           ; Wait ~50ms, returns non-zero in A if ESC pressed
        ORA     A               ; Test A register (0 = timeout, non-zero = ESC)
        JNZ     DONE            ; If ESC pressed, exit program

        ; Increment loop number for next iteration
        LHLD    LOPNUM          ; Load current loop number into HL
        INX     H               ; Increment HL by 1
        SHLD    LOPNUM          ; Store updated loop number

        ; Decrement remaining loop count
        LHLD    LOPCNT          ; Load remaining count into HL
        DCX     H               ; Decrement HL by 1
        SHLD    LOPCNT          ; Store updated remaining count
        JMP     MAINLP          ; Jump back to start of main loop

; ============================================================================
; PROGRAM TERMINATION
; ============================================================================
DONE:                           ; Clean exit point
        MVI     C,0             ; Load C with BDOS function 0 (system reset/exit)
        CALL    BDOS            ; Transfer control back to CP/M (does not return)

; ============================================================================
; STRING AND CHARACTER OUTPUT ROUTINES
; ============================================================================

; ----------------------------------------------------------------------------
; PRSTR - Print String via BDOS
; ----------------------------------------------------------------------------
; Prints a '$'-terminated string using CP/M BDOS function 9.
;
; Input:   DE = Address of string (must end with '$' character)
; Output:  None
; Affects: All registers may be corrupted by BDOS call
; ----------------------------------------------------------------------------
PRSTR:
        MVI     C,9             ; BDOS function 9 = Print String
        CALL    BDOS            ; Call BDOS (prints string at DE until '$')
        RET                     ; Return to caller

; ----------------------------------------------------------------------------
; PRCHR - Print Single Character via BDOS
; ----------------------------------------------------------------------------
; Prints a single character using CP/M BDOS function 2 (console output).
;
; Input:   A = Character to print
; Output:  Character displayed on console
; Affects: All registers may be corrupted by BDOS call
; ----------------------------------------------------------------------------
PRCHR:
        MOV     E,A             ; Copy character from A to E (BDOS expects it in E)
        MVI     C,2             ; BDOS function 2 = Console Output
        CALL    BDOS            ; Call BDOS to print character
        RET                     ; Return to caller

; ============================================================================
; DECIMAL NUMBER PRINTING ROUTINES
; ============================================================================

; ----------------------------------------------------------------------------
; PRDEC - Print Decimal Number
; ----------------------------------------------------------------------------
; Converts a 16-bit binary number in HL to decimal and prints it.
; Suppresses leading zeros (e.g., 42 prints as "42", not "00042").
; Handles numbers from 0 to 65535.
;
; Algorithm:
;   1. Extract each digit by repeated division (10000, 1000, 100, 10)
;   2. Use OUTDIG to print each digit (handles leading zero suppression)
;   3. Always print the ones digit (even if zero)
;
; Input:   HL = 16-bit number to print (0-65535)
; Output:  Decimal representation printed to console
; Affects: All registers
; Uses:    D register to track leading zero state
;          BC register pairs for divisors
;          Calls GETDG to extract each digit
;          Calls OUTDIG to print each digit
; ----------------------------------------------------------------------------
PRDEC:
        PUSH    B               ; Preserve BC (BDOS corrupts it, we reuse BC for divisors)
        MVI     D,0             ; D = leading zero flag (0 = haven't printed non-zero yet)
        
        ; Extract and print ten-thousands digit (10000s place)
        LXI     B,10000         ; BC = 10000 (divisor for ten-thousands)
        CALL    GETDG           ; Extract digit, returns in A, updates HL to remainder
        PUSH    H               ; Save remainder (HL) - BDOS in OUTDIG may corrupt it
        CALL    OUTDIG          ; Print digit (may update D to 1 if non-zero printed)
        POP     H               ; Restore remainder for next digit extraction
        
        ; Extract and print thousands digit (1000s place)
        LXI     B,1000          ; BC = 1000 (divisor for thousands)
        CALL    GETDG           ; Extract digit, returns in A, updates HL to remainder
        PUSH    H               ; Save remainder
        CALL    OUTDIG          ; Print digit
        POP     H               ; Restore remainder
        
        ; Extract and print hundreds digit (100s place)
        LXI     B,100           ; BC = 100 (divisor for hundreds)
        CALL    GETDG           ; Extract digit, returns in A, updates HL to remainder
        PUSH    H               ; Save remainder
        CALL    OUTDIG          ; Print digit
        POP     H               ; Restore remainder
        
        ; Extract and print tens digit (10s place)
        LXI     B,10            ; BC = 10 (divisor for tens)
        CALL    GETDG           ; Extract digit, returns in A, updates HL to remainder
        PUSH    H               ; Save remainder
        CALL    OUTDIG          ; Print digit
        POP     H               ; Restore remainder
        
        ; Print ones digit (always printed, even if zero)
        MOV     A,L             ; L now contains the ones digit (0-9)
        ADI     '0'             ; Convert binary digit to ASCII ('0'-'9')
        CALL    PRCHR           ; Print the ones digit
        
        POP     B               ; Restore original BC
        RET                     ; Return to caller

; ----------------------------------------------------------------------------
; OUTDIG - Output Single Digit with Leading Zero Suppression
; ----------------------------------------------------------------------------
; Prints a single decimal digit, but suppresses it if it's a leading zero.
; Uses register D as a flag to track whether we've printed any non-zero digit.
;
; Logic:
;   - If digit is non-zero: print it and set D=1
;   - If digit is zero and D=0: suppress (it's a leading zero)
;   - If digit is zero and D=1: print it (not a leading zero)
;
; Input:   A = Digit value (0-9)
;          D = Leading zero flag (0 = no non-zero printed yet, 1 = printed)
; Output:  Digit printed to console (unless suppressed)
;          D = Updated to 1 if digit was printed
; Affects: A, E, C (via PRCHR and BDOS calls)
; ----------------------------------------------------------------------------
OUTDIG:
        MOV     E,A             ; Save digit to E (in case we need it later)
        ORA     A               ; Test if digit is zero (sets flags)
        JNZ     ODDO            ; If non-zero, jump to print it
        
        ; Digit is zero - check if it's a leading zero
        MOV     A,D             ; Load leading zero flag into A
        ORA     A               ; Test if we've printed a non-zero yet
        RZ                      ; If D=0 (no non-zero yet), return (suppress this zero)
        
        ; Not a leading zero - restore digit and print it
        MOV     A,E             ; Restore digit from E to A
        
ODDO:                           ; Output digit
        ADI     '0'             ; Convert binary digit (0-9) to ASCII ('0'-'9')
        CALL    PRCHR           ; Print the character
        MVI     D,1             ; Set flag: we've now printed a non-zero digit
        RET                     ; Return to caller

; ----------------------------------------------------------------------------
; GETDG - Get Digit by Division
; ----------------------------------------------------------------------------
; Extracts a single decimal digit by counting how many times the divisor (BC)
; fits into the dividend (HL). This is essentially: digit = HL / BC,
; with HL updated to the remainder (HL = HL % BC).
;
; Algorithm:
;   1. Initialize counter (E) to 0
;   2. While HL >= BC:
;      - Subtract BC from HL
;      - Increment counter
;   3. Return counter (the digit) in A
;   4. HL now contains remainder for next digit extraction
;
; Example: HL=1234, BC=100 -> A=12 (digit), HL=34 (remainder)
;
; Input:   HL = Dividend (number to extract digit from)
;          BC = Divisor (place value: 10000, 1000, 100, 10)
; Output:  A = Digit extracted (0-9, or 0-6 for 10000s place)
;          HL = Remainder after division (HL mod BC)
; Affects: A, E, HL
; ----------------------------------------------------------------------------
GETDG:
        MVI     E,0             ; E = digit counter (initialize to 0)
        
GDLOP:                          ; Main division loop
        ; Compare HL with BC to see if we can subtract
        MOV     A,H             ; Load high byte of HL into A
        CMP     B               ; Compare with high byte of BC
        JC      GDDNE           ; If H < B, we're done (HL < BC)
        JNZ     GDSUB           ; If H > B, definitely subtract (HL > BC)
        
        ; High bytes equal, check low bytes
        MOV     A,L             ; Load low byte of HL into A
        CMP     C               ; Compare with low byte of BC
        JC      GDDNE           ; If L < C, we're done (HL < BC)
        
GDSUB:                          ; Subtract BC from HL
        ; Perform 16-bit subtraction: HL = HL - BC
        MOV     A,L             ; Load low byte of HL
        SUB     C               ; Subtract low byte of BC
        MOV     L,A             ; Store result back in L
        
        MOV     A,H             ; Load high byte of HL
        SBB     B               ; Subtract high byte of BC with borrow
        MOV     H,A             ; Store result back in H
        
        INR     E               ; Increment digit counter
        JMP     GDLOP           ; Continue loop
        
GDDNE:                          ; Division complete
        MOV     A,E             ; Move digit result from E to A
        RET                     ; Return to caller (A = digit, HL = remainder)

; ============================================================================
; TIMING AND DELAY ROUTINES
; ============================================================================

; ----------------------------------------------------------------------------
; WAIT5 - Wait with ESC Key Detection
; ----------------------------------------------------------------------------
; Waits for approximately 50 milliseconds while checking for ESC key press.
; The delay is implemented as multiple 50ms chunks, checking for keyboard
; input between chunks to allow user interruption.
;
; Timing: 1 chunk × 50ms = 50ms total
;
; Input:   None
; Output:  A = 0 if timeout completed normally
;          A = 1 if ESC key was pressed (early exit)
; Affects: A, C, E (via BDOS calls)
; Uses:    CHCNT memory location for chunk counter
; ----------------------------------------------------------------------------
WAIT5:
        MVI     A,1             ; A = 1 (number of 50ms chunks to wait)
        STA     CHCNT           ; Store chunk count in memory
        
W5LOOP:                         ; Main wait loop
        CALL    W0100           ; Wait for one 50ms interval
        
        ; Check for ESC key press using BDOS function 6 (direct console I/O)
        MVI     E,0FFH          ; E = 0FFH (console input, no wait)
        MVI     C,6             ; C = 6 (BDOS function 6: Direct Console I/O)
        CALL    BDOS            ; Check if key pressed (returns char in A, or 0)
        CPI     1BH             ; Compare with 1BH (ESC key ASCII code)
        JZ      W5ESC           ; If ESC pressed, exit with error code
        
        ; Decrement chunk counter and check if done
        LDA     CHCNT           ; Load chunk counter from memory
        DCR     A               ; Decrement by 1
        STA     CHCNT           ; Store back to memory
        JNZ     W5LOOP          ; If not zero, continue waiting
        
        ; Timer completed normally
        XRA     A               ; A = 0 (clear A by XOR with itself)
        RET                     ; Return with A = 0 (success)
        
W5ESC:                          ; ESC key was pressed
        MVI     A,1             ; A = 1 (signal early exit)
        RET                     ; Return with A = 1 (ESC pressed)

; ----------------------------------------------------------------------------
; W0100 - Wait 50 Milliseconds Using Hardware Timer
; ----------------------------------------------------------------------------
; Uses countdown timer 2 through port 23 to create an accurate 50ms delay.
;
; Timer Operation:
;   1. Write start command 12H, then duration low/high bytes (50, 0)
;   2. Write query command 22H to select timer 2
;   3. Poll: 1 = running, 0 = expired, other values = interface error
; Arming is synchronous; the first read may already report expiry.
;
; Input:   None
; Output:  None (delays for ~50ms)
; Affects: A
; ----------------------------------------------------------------------------
W0100:
        MVI     A,TMRSET
        OUT     TMRIO
        MVI     A,50            ; Duration low byte
        OUT     TMRIO
        XRA     A               ; Duration high byte; final write arms timer
        OUT     TMRIO
        MVI     A,TMRGET
        OUT     TMRIO

W1CHK:
        IN      TMRIO
        CPI     2
        JNC     TMRERR
        ORA     A
        JNZ     W1CHK
        
        RET                     ; Return to caller (50ms has elapsed)

TMRERR:
        IN      TMRIO
        XRA     A
        OUT     TMRIO
        LXI     D,TMRMSG
        CALL    PRSTR
        JMP     DONE

; ============================================================================
; DATA STORAGE AREA
; ============================================================================
; This section contains variables and constants used by the program.
; All data is located after the code to avoid execution.
; ============================================================================

LOPCNT:         DW      0000H   ; Remaining loop counter (16-bit)
                                ; Decrements from 60000 to 0
                                
LOPNUM:         DW      0000H   ; Current loop number (16-bit)
                                ; Increments from 1 to 60000
                                ; This is the value displayed in the message
                                
CHCNT:          DB      00H     ; Chunk counter for delay routine (8-bit)
                                ; Used by WAIT5 to track 50ms intervals

; Message text - must end with '$' (CP/M string terminator)
MSGTXT:         DB      'Vibe coded with OpenAI Codex - press esc to exit - loop number ', '$'
TMRMSG:         DB      'Timer interface error',0DH,0AH,'$'

; ============================================================================
; PROGRAM END
; ============================================================================
        END     START           ; End of assembly, entry point is START
