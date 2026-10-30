/* dcc stack ABI -> public BIOS register ABI. No port or private workspace
 * addresses belong here. IX/IY survive; HL follows each BIOS operation.
 */
#include "BIOSBANK.H"

unsigned int bmres;

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
	call 0ff10h
	jp _bmcap
;@dcc.lto end _bmsize
;@dcc.lto begin _bmavl
	public _bmavl
_bmavl:
	ld e,3
	call 0ff10h
	jp _bmcap
;@dcc.lto end _bmavl
;@dcc.lto begin _bmrset
	public _bmrset
_bmrset:
	ld e,4
	call 0ff10h
	jp _bmcap
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
	call 0ff10h
	jp _bmcap
;@dcc.lto end _bmexec

; E=6: ordinary DCC C function entry at code+IX. IY is copied to the
; selected bank-1 stack as the function's one pointer argument.
;@dcc.lto begin _bmcall
	public _bmcall
_bmcall:
	push ix
	push iy
	ld ix,0
	add ix,sp
	ld e,(ix+6)
	ld d,(ix+7)
	ld c,(ix+8)
	ld b,(ix+9)
	ld l,(ix+10)
	ld h,(ix+11)
	push hl
	ld l,(ix+12)
	ld h,(ix+13)
	push hl
	pop iy
	pop ix
	ex de,hl
	ld e,6
	call 0ff10h
	call _bmcap
	pop iy
	pop ix
	ret
;@dcc.lto end _bmcall

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
	call 0ff10h
	jp _bmcap
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
	call 0ff10h
	jp _bmcap
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
	ld (_bmres),a
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
	ld (_bmres),a
	ld hl,0
	jr c,BMW_END
	inc l
BMW_END:
	pop ix
	ret
;@dcc.lto end _bmwrit
;@dcc.lto begin _bmcap
	public _bmcap
_bmcap:
	ld a,h
	or l
	jr z,BMC_ERROR
	push hl
	ld hl,0
	jr BMC_STORE
BMC_ERROR:
	push hl
	ld e,11
	call 0ff10h
BMC_STORE:
	ld (_bmres),hl
	pop hl
	ret
;@dcc.lto end _bmcap
;@dcc.lto begin _bmerr
	public _bmerr
_bmerr:
	ld hl,(_bmres)
	ret
;@dcc.lto end _bmerr
;@dcc.lto begin _bmqury
	public _bmqury
_bmqury:
	push ix
	ld ix,0
	add ix,sp
	ld e,9
	call 0ff10h
	ld a,h
	or l
	jr z,BMQ_ERROR
	ex de,hl
	ld l,(ix+4)
	ld h,(ix+5)
	ld bc,48
	call 0ff00h
	ld (_bmres),a
	ld hl,0
	jr c,BMQ_END
	inc l
BMQ_END:
	pop ix
	ret
BMQ_ERROR:
	call _bmcap
	jr BMQ_END
;@dcc.lto end _bmqury
;@dcc.lto begin _bmctrl
	public _bmctrl
_bmctrl:
	push ix
	push iy
	ld ix,0
	add ix,sp
	ld e,(ix+6)
	ld d,(ix+7)
	ld l,(ix+8)
	ld h,(ix+9)
	ld bc,32
	call 0ff03h
	jr c,BMCT_IO
	ld l,(ix+6)
	ld h,(ix+7)
	ld e,10
	call 0ff10h
	push hl
	ld e,(ix+6)
	ld d,(ix+7)
	ld l,(ix+8)
	ld h,(ix+9)
	ld bc,32
	call 0ff00h
	pop de
	jr c,BMCT_IO
	ld (_bmres),de
	ld a,d
	or e
	ld hl,0
	jr nz,BMCT_END
	inc l
	jr BMCT_END
BMCT_IO:
	ld hl,9
	ld (_bmres),hl
	ld hl,0
BMCT_END:
	pop iy
	pop ix
	ret
;@dcc.lto end _bmctrl
;@dcc.lto begin _bmrbyt
	public _bmrbyt
_bmrbyt:
	push ix
	ld ix,0
	add ix,sp
	ld e,(ix+4)
	ld d,(ix+5)
	ld l,(ix+6)
	ld h,(ix+7)
	ld c,(ix+8)
	ld b,(ix+9)
	call 0f800h
	ld (_bmres),a
	ld hl,0
	jr c,BMRB_END
	inc l
BMRB_END:
	pop ix
	ret
;@dcc.lto end _bmrbyt
;@dcc.lto begin _bmwbyt
	public _bmwbyt
_bmwbyt:
	push ix
	ld ix,0
	add ix,sp
	ld e,(ix+4)
	ld d,(ix+5)
	ld l,(ix+6)
	ld h,(ix+7)
	ld c,(ix+8)
	ld b,(ix+9)
	call 0f803h
	ld (_bmres),a
	ld hl,0
	jr c,BMWB_END
	inc l
BMWB_END:
	pop ix
	ret
;@dcc.lto end _bmwbyt
#endasm
