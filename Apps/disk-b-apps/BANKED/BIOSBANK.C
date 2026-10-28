/* dcc stack ABI -> public BIOS register ABI. No port or private workspace
 * addresses belong here. IX/IY survive; HL follows each BIOS operation.
 */
#include "BIOSBANK.H"

#asm
;@dcc.lto begin _bmsize
	public _bmsize
_bmsize:
	ld hl,2
	add hl,sp
	ld e,(hl)
	inc hl
	ld d,(hl)
	ex de,hl
	ld e,2
	jp 0ff10h
;@dcc.lto end _bmsize
;@dcc.lto begin _bmavl
	public _bmavl
_bmavl:
	ld e,3
	jp 0ff10h
;@dcc.lto end _bmavl
;@dcc.lto begin _bmrset
	public _bmrset
_bmrset:
	ld e,4
	jp 0ff10h
;@dcc.lto end _bmrset

;@dcc.lto begin _bmexec
	public _bmexec
_bmexec:
	ld hl,2
	add hl,sp
	ld e,(hl)
	inc hl
	ld d,(hl)
	inc hl
	ld c,(hl)
	inc hl
	ld b,(hl)
	ex de,hl
	ld e,5
	jp 0ff10h
;@dcc.lto end _bmexec

;@dcc.lto begin _bmfree
	public _bmfree
_bmfree:
	ld hl,2
	add hl,sp
	ld e,(hl)
	inc hl
	ld d,(hl)
	ex de,hl
	ld e,1
	jp 0ff10h
;@dcc.lto end _bmfree
;@dcc.lto begin _bmaloc
	public _bmaloc
_bmaloc:
	ld hl,2
	add hl,sp
	ld e,(hl)
	inc hl
	ld d,(hl)
	ex de,hl
	ld e,0
	jp 0ff10h
;@dcc.lto end _bmaloc
;@dcc.lto begin _bmstat
	public _bmstat
_bmstat:
	call 0ff06h
	ret
;@dcc.lto end _bmstat
;@dcc.lto begin _bmread
	public _bmread
_bmread:
	push ix
	ld ix,0
	add ix,sp
	ld e,(ix+4)
	ld d,(ix+5)
	ld l,(ix+6)
	ld h,(ix+7)
	ld c,(ix+8)
	ld b,(ix+9)
	call 0ff00h
	ld hl,0
	jr c,BMR_END
	inc l
BMR_END:
	pop ix
	ret
;@dcc.lto end _bmread
;@dcc.lto begin _bmwrit
	public _bmwrit
_bmwrit:
	push ix
	ld ix,0
	add ix,sp
	ld e,(ix+4)
	ld d,(ix+5)
	ld l,(ix+6)
	ld h,(ix+7)
	ld c,(ix+8)
	ld b,(ix+9)
	call 0ff03h
	ld hl,0
	jr c,BMW_END
	inc l
BMW_END:
	pop ix
	ret
;@dcc.lto end _bmwrit
#endasm
