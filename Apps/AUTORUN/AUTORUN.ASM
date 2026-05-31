;*****************************************************************************
;
;  AutoRun - Configure auto-run command line for Altair (Burcon) CP/M 2.2
;
;	This program allows the user to specify an auto-run command line
;	for Altair (Burcon) CP/M 2.2. In addition to specifying the command
;	line, the user can choose to enable the command to process on
;	cold boot only, warm boot only, both cold and warm boot, or to
;	disable the auto-run command line.
;
;	Written by Mike Douglas, February 2014
;
;	Rev	 Date	  Desc
;	1.0	2/17/14   Original
;
;*****************************************************************************

; BDOS and misc equates

RDCON	equ	1		;BDOS read console character 
PRINT	equ	9		;BDOS write string to console
RDLINE	equ	10		;BDOS read a line from the console
BDOS	equ	5		;BDOS entry point
BIOS	equ	1		;address of BIOS stored on page zero
IBUFLEN	equ	80		;length of input buffer
CR	equ	13		;ascii for carriage return
LF	equ	10		;ascii for line feed

; Location of data to change for auto-run in the CP/M boot tracks

CMDTRK	equ	0		;command in CCP on track 0
CMDSEC	equ	4		;command in CCP on sector 4 (1 indexed)
FLAGTRK	equ	1		;flag byte in BIOS on track 1
FLAGSEC	equ	26		;flag byte in BIOS on sector 26 (1 idx'd)

;  BIOS Jump Table Offsets. This table is used to compute BIOS entry
;     points by adding the symbols below to the warm boot entry address
;     stored by CP/M at location 1 on page zero.
	
	org	0
bWboot	ds	3		;warm start
bConSt	ds	3		;console status
bConIn	ds	3		;console character in 
bConOut	ds	3		;console character out 
bList	ds	3		;list character out 
bPunch	ds	3		;punch character out 
bReader	ds	3		;reader character out
bHome	ds	3		;move head to home position
bSelDsk	ds	3		;select disk
bSetTrk	ds	3		;set track number
bSetSec	ds	3		;set sector number
bSetDma	ds	3		;set dma address
bRead	ds	3		;read disk
bWrite	ds	3		;write disk


	org	0100h		;CP/M load and entry address
;-----------------------------------------------------------------------------
;
;  Program entry point
;
;-----------------------------------------------------------------------------
	lxi	h,0		;save stack pointer by HL=SP+0
	dad	sp		;HL=current stack pointer from CP/M
	shld	stack		;save for restore later
	lxi	sp,ourStk	;setup our own stack
	xra	a		;set the updated flag to false
	sta	updated
	
; Prompt for and input drive letter to use

	lxi	d,mDrive	;display drive message
	call	dispMsg
	call	readChr		;read response
	ani	5fh		;clear bit 5 (a to A)
	cpi	'A'		;validate A-D
	jc	exitNo		;invalid, exit with no update
	cpi	'E'
	jnc	exitNo
	sui	'A'		;convert A-D to 0-3
	call	selDsk		;select specified drive

; Read the sector which contains the command line string into cmdBuf

	mvi	a,CMDTRK	;command string track
	call	setTrk
	mvi	a,CMDSEC	;command string sector
	call	setSec
	lxi	b,cmdBuf	;sector buffer address
	call	setDma
	call	dRead		;read the command string sector
	jnz	readErr

; Read the sector which contains the flag byte into flagBuf

	mvi	a,FLAGTRK	;flag byte track
	call	setTrk
	mvi	a,FLAGSEC	;flag byte sector
	call	setSec
	lxi	b,flgBuf	;sector buffer address
	call	setDma
	call	dRead		;read the flag byte sector
	jnz	readErr

; Display auto-run configuration menu. Read response and process.

	lda	flgByte		;current flag (mode) byte from sector
	ani	03		;get flag bits alone
	jnz	addAsci		;if not zero, go add ascii offset
	mvi	a,4
addAsci	adi	'0'		;add ascii offset for digits
	sta	mCurFlg		;store printable flag value
	lxi	d,mMenu		;DE=address of menu message
	call	dispMsg		;display menu
	call	readChr		;read response
	cpi	'1'		;cold boot only
	jz	haveOpt		
	cpi	'2'		;warm boot only
	jz	haveOpt
	cpi	'3'		;cold and warm boot
	jz	haveOpt
	cpi	'4'		;disable
	jz	haveOpt
	ora	a		;if zero, RETURN was pressed
	jnz	exitNo		;something else entered, just exit
	lda	flgByte		;use the same value as before

; haveOpt - have a valid menu selection. Save the selection and then
;    display the current command string unless "disable" chosen above.

haveOpt	ani	03h		;save lower two bits
	sta	flgBits
	jz	wrtFlag		;zero=disable, go write flag sector to disk
	lxi	d,mCurCmd	;DE=address of current command string prompt 
	call	dispMsg		;"Current command: "
	lxi	d,mNone		;assume no command present
	lda	cmdLen		;A=length of current command
	ora	a		;test for zero length command
	jz	dispCmd		;display command string "(none")

; Copy current command to inBuf and then display it

	lxi	h,cmdStr	;HL=source from sector buffer
	lxi	d,inBuf		;DE=destination
	mov	b,a		;B=string length
copy1	mov	a,m		;move (HL) to (DE)
	stax	d
	inx	h
	inx	d
	dcr	b		;decrement character count
	jnz	copy1
	mvi	a,'$'		;terminating $
	stax	d
	lxi	d,inBuf		;display from inBuf
dispCmd	call	dispMsg

; Prompt for and input new command string

	lxi	d,mNewCmd	;input new command prompt
	call	dispMsg
	lxi	d,inBuf		;DE=pointer to input buffer
	mvi	a,IBUFLEN	;store max length to read
	stax	d
	mvi	c,RDLINE	;BDOS command, read line
	call	BDOS
	lda	inBuf+1		;A=length read
	ora	a		;test for zero length string
	jz	wrtFlag		;go write just flag sector to disk

; Copy the new command string into the command sector

	sta	updated		;set updated flag to true
	sta	cmdLen		;store new command length
	mov	b,a		;B=string length
	lxi	d,cmdStr	;DE=destination in sector buffer
	lxi	h,inBuf+2	;HL=source from user entry
copy2	mov	a,m		;move (HL) to (DE)
	call	toUpper		;convert to upper case
	stax	d
	inx	h
	inx	d
	dcr	b		;decrement character count
	jnz	copy2
	xra	a		;store terminating null
	stax	d

; wrtCmd - write command string sector to disk

wrtCmd	mvi	a,CMDTRK	;command string track
	call	setTrk
	mvi	a,CMDSEC	;command string sector
	call	setSec
	lxi	b,cmdBuf	;sector buffer address
	call	setDma
	call	dWrite		;write the command string sector
	jnz	wrtErr

; wrtFlag - write flag byte sector to disk

wrtFlag	lxi	d,flgByte	;DE=address of flag byte in disk sector
	lxi	h,flgBits	;HL=address of new flag bits
	ldax	d		;A=flag byte from disk
	ani	03		;get option bits alone
	cmp	m		;compare to new value
	jz	exitYes		;exit, yes update complete

; Flag byte has changed. OR in the new bits and save the sector

	inr	a		;A=non-zero value
	sta	updated		;set updated flag true
	ldax	d		;A=flag byte from disk
	ani	0fch		;clear lower two bits
	ora	m		;or in new bits
	stax	d		;store in sector	
	mvi	a,FLAGTRK	;flag byte track
	call	setTrk
	mvi	a,FLAGSEC	;flag byte sector
	call	setSec
	lxi	b,flgBuf	;sector buffer address
	call	setDma
	call	dWrite		;write the flag byte sector
	jnz	wrtErr
	jmp	exitYes

;----------------------------------------------------------------------
;  Support routines
;----------------------------------------------------------------------

; toUpper - convert A to upper case

toUpper	cpi	'a'		;look for letters from 'a' to 'z'
	rc			;before 'a'
	cpi	'{'		;after 'z'
	rnc
	ani	5fh		;convert to upper case
	ret

; dispMsg - use BDOS print message command to display the string
;    pointed to by DE.

dispMsg	mvi	c,PRINT		;BDOS command to display a string
	jmp	BDOS		;call BDOS

; readChr - read and return a single character but used the BDOS input
;   line command for "normal" input look and feel. Return zero if no
;   input entered (e.g., just return pressed).

readChr	lxi	d,inBuf		;DE=pointer to input buffer
	mvi	a,IBUFLEN	;store max length to read
	stax	d
	mvi	c,RDLINE	;BDOS command, read line
	call	BDOS
	lda	inBuf+1		;get length read
	ora	a		;see if nothing entered
	rz			;if so, return zero
	lda	inBuf+2		;return first character in the line
	ret

;-------------------------------------------------------------------
;  BIOS function calls. Single byte parameters all come in A and
;     are moved to C as needed.
;-------------------------------------------------------------------

selDsk	mov	c,a		;selDsk wants param in c
	lhld	BIOS		;HL=warm start entry in BIOS
	lxi	d,bSelDsk	;DE=offset of select disk function
	dad	d
	pchl			;jump to select disk function

setTrk	mov	c,a		;setTrk wants param in c
	lhld	BIOS		;HL=warm start entry in BIOS
	lxi	d,bSetTrk	;DE=offset of set track function
	dad	d
	pchl			;jump to set track function

setSec	mov	c,a		;setSec wants param in c
	lhld	BIOS		;HL=warm start entry in BIOS
	lxi	d,bSetSec	;DE=offset of set sector function
	dad	d
	pchl			;jump to set sector function

setDma	lhld	BIOS		;HL=warm start entry in BIOS
	lxi	d,bSetDma	;DE=offset of set dma function
	dad	d
	pchl			;jump to set dma function (dma addr in bc)

dRead	lhld	BIOS		;HL=warm start entry in BIOS
	lxi	d,bRead		;DE=offset of disk read function
	dad	d
	pchl			;jump to disk read function

dWrite	lhld	BIOS		;HL=warm start entry in BIOS
	lxi	d,bWrite	;DE=offset of disk write function
	dad	d
	pchl			;jump to disk read function

;-------------------------------------------------------------------
;  Exit routines
;-------------------------------------------------------------------

exitYes	lxi	d,mSaved	;assume changes were made
	lda	updated		;was the disk updated?
	ora	a
	jnz	exit		;yes, display changes made message
				;otherwise, fall through
exitNo	lxi	d,mExitNo	;exit with no changes saved
	jmp	exit

readErr	lxi	d,mRdErr	;exit with read error
	jmp	exit

wrtErr	lxi	d,mWrtErr	;exit with write error
	jmp	exit

exit	mvi	c,PRINT		;display message passed in DE
	call	BDOS
	lhld	stack		;restore stack pointer
	sphl
	ret			;return to CCP 

;-------------------------------------------------------------------
;  String Constants
;-------------------------------------------------------------------

mDrive	db	CR,LF,' *** Configure CP/M Auto-Run Command ***',CR,LF
	db	      '    (For Altair/Burcon CP/M 2.2 Only)',CR,LF,LF
	db	'Enter letter of drive to update: $'

mMenu	db	CR,LF,LF,'Current Auto-Run Option is ('
mCurFlg	db	'1)',CR,LF,LF
	db	'  1) Run on Cold Boot Only',CR,LF
	db	'  2) Run on Warm Boot Only',CR,LF
	db	'  3) Run on Cold and Warm Boot',CR,LF
	db	'  4) Disable Auto-Run Command', CR,LF
	db	'  Q) Quit with No Changes',CR,LF,LF
	db	'Enter New Option (RETURN for no change): $'

mCurCmd	db	CR,LF,LF,'Current command string: $'
mNone	db	'(none)$'
mNewCmd	db	CR,LF,'New command (RETURN for no change): $'

mExitNo	db	CR,LF,LF,'No changes made.',CR,LF,'$'
mSaved	db	CR,LF,LF,'Changes successfully saved to disk.',CR,LF
	db	'Reboot for changes to properly take effect.',CR,LF,'$'
mRdErr	db	CR,LF,LF,'Not able to read disk.',CR,LF,'$'
mWrtErr	db	CR,LF,LF,'Not able to write to disk.',CR,LF,'$'

;-------------------------------------------------------------------
;  Data Area
;-------------------------------------------------------------------

; 128 byte buffer for the command string sector

cmdBuf	ds	7		;command sector buffer (128 bytes)
cmdLen	ds	1		;command string length
cmdStr	ds	120		;command string

; 128 byte buffer for the flag byte sector

flgBuf	ds	89		;flag sector buffer (128 bytes)
flgByte	ds	1		;flag (mode) byte
	ds	38		;rest of buffer

; Misc variables

stack	ds	2		;save area for CP/M stack pointer
updated	ds	1		;flag for whether disk was updated
flgBits	ds	1		;bits 0,1 for flag byte 
inBuf	ds	IBUFLEN		;storage for command string
	ds	64		;our stack
ourStk	equ	$

	end
