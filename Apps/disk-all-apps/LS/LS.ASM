; altair-z80 standalone
; CP/M 2.2 LS. Listing logic was originally emitted by DCC.
; Self-contained startup and helpers: no C runtime or warm-boot exit.
;
; The bundled CCP calls 0100H with a return address on its stack.
; Preserve that stack and use private RAM below the resident CCP, then RET.
; __bsse and LSSTK reserve scratch RAM without adding bytes to the COM.
        cseg
        public LSENT
LSENT:
        ld (LSSP),sp
        ld sp,LSSTK
        xor a
        ld hl,__bssb
        ld (hl),a
        ld de,__bssb+1
        ld bc,__bssn-1
        ldir
        call LSARGS
        jr c,LSDONE
        call _main
LSDONE:
        ld de,0080h
        ld c,26                 ; restore the CCP's default DMA address
        call 5
        ld sp,(LSSP)
        ret                     ; resume the resident CCP, no JP 0 / BDOS 0
LSSP:   dw 0

;@dcc.lto begin _main

	public _main
_main:
	push ix
	ld ix,0
	add ix,sp
	ld hl,-26
	add hl,sp
	ld sp,hl
	ld (ix-12),0
	ld (ix-11),0
L200:
	ld c,25
	call __bdosf           ; byte result is already zero-extended in HL
	ld (ix-24),l
	ld hl,92
	ld l,(hl)
	ld h,0
	ld (ix-14),l
	ld (ix-13),h
	ld a,h
	or l
	jr z,L211 ; peep: jp_to_jr
	ld l,(ix-14)
	ld h,(ix-13)
	ld de,1
	or a
	sbc hl,de
	push hl
	ld hl,14
	ld c,l
	pop de
	call __bdosf
L211:
	ld hl,31
	ld de,0 ; peep: push_lde_pop
	ld c,l
	call __bhlf
	ld (ix-4),l
	ld (ix-3),h
	ld de,5
	add hl,de
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	inc hl ; peep: ld_de1_to_inc
	ld (ix-8),l
	ld (ix-7),h
	ld l,(ix-4)
	ld h,(ix-3)
	ld de,7
	add hl,de
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	inc hl ; peep: ld_de1_to_inc
	ld (ix-10),l
	ld (ix-9),h
	ld l,(ix-4)
	ld h,(ix-3)
	ld de,9
	add hl,de
	ld l,(hl)
	ld h,l
	ld l,0
	push hl
	ld l,(ix-4)
	ld h,(ix-3)
	ld de,10
	add hl,de
	ld l,(hl)
	ld h,0
	push hl
	pop de
	pop hl
	ld a,h
	or d
	ld h,a
	ld a,l
	or e
	ld l,a
	push hl
	call _Z0007
	pop bc
	ld (ix-21),l
	ld l,(ix-4)
	ld h,(ix-3)
	inc hl ; peep: ld_de2_to_inc
	inc hl
	ld l,(hl)
	ld h,0
	ld de,3
	or a
	sbc hl,de
	ld (ix-22),l
	ld c,25
	call __bdosf           ; byte result is already zero-extended in HL
	ld (ix-23),l
	ld hl,32
	ld de,255 ; peep: push_lde_pop
	ld c,l
	call __bdosf
	ld de,255 ; peep: push_lde_pop
	ld a,h
	and d
	ld a,l
	and e
	ld l,a
	ld h,0
	ld (ix-14),l
	ld (ix-13),h
	ld l,(ix-8)
	ld h,(ix-7)
	ld de,256
	ex de,hl
	or a
	sbc hl,de
	ld hl,0
	jr nc,L221 ; peep: jp_to_jr
	inc l
L221:
	ld (ix-16),l
	ld (ix-15),h
	push ix
	pop hl
	ld de,-12
	add hl,de
	push hl
	ld l,(ix-16)
	ld h,(ix-15)
	push hl
	ld l,(ix-14)
	ld h,(ix-13)
	push hl
	call _Z0014
	pop bc
	pop bc
	pop bc
	ld a,h
	or l
	jr nz,L213 ; peep: jp_to_jr
	ld l,(ix-24)
	ld h,0
	push hl
	ld hl,14
	ld c,l
	pop de
	call __bdosf
	ld hl,S3
	push hl
	call _Z0006
	pop bc
	ld hl,1
	jp L222
L213:
	ld hl,27
	ld de,0 ; peep: push_lde_pop
	ld c,l
	call __bhlf
	ld (ix-6),l
	ld (ix-5),h
	ld l,(ix-21)
	ld h,0
	push hl
	ld l,(ix-8)
	ld h,(ix-7)
	push hl
	ld l,(ix-6)
	ld h,(ix-5)
	push hl
	call _Z0008
	pop bc
	pop bc
	pop bc
	ld (ix-14),l
	ld (ix-13),h
	ld l,(ix-21)
	ld h,0
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,(ix-8)
	ld h,(ix-7)
	or a
	sbc hl,de
	ld (ix-16),l
	ld (ix-15),h
	ld e,(ix-22) ; peep: ix_byte_load_to_de
	ld d,0
	ld b,e
	ld a,b
	or a
	jr z,L224 ; peep: jp_to_jr
L223:
	add hl,hl
	djnz L223
L224:
	ld (ix-16),l
	ld (ix-15),h
	ld l,(ix-14)
	ld h,(ix-13)
	ld e,(ix-22) ; peep: ix_byte_load_to_de
	ld d,0
	ld b,e
	ld a,b
	or a
	jr z,L226 ; peep: jp_to_jr
L225:
	add hl,hl
	djnz L225
L226:
	ld (ix-14),l
	ld (ix-13),h
	ld l,(ix-24)
	ld h,0
	push hl
	ld hl,14
	ld c,l
	pop de
	call __bdosf
	ld a,(_Z0023)
	or a
	ld hl,0
	jr z,L227 ; peep: jp_to_jr
	inc hl
L227:
	ld a,h
	or l
	jr z,L216 ; peep: jp_to_jr
	ld a,(_Z0012)
	ld l,a
	ld h,0
	push hl
	ld hl,_Z0013
	push hl
	call _Z0001
	pop bc
	pop bc
L216:
	ld l,(ix-22)
	ld h,0
	push hl
	call _Z0021
	pop bc
	ld a,(_Z0012)
	ld l,a
	ld h,0
	push hl
	call _Z0017
	pop bc
	ld hl,S4
	push hl
	call _Z0006
	pop bc
	ld l,(ix-16)
	ld h,(ix-15)
	ld e,(ix-14) ; peep: ix_pair_load_to_de
	ld d,(ix-13)
	or a
	sbc hl,de
	push hl
	call _Z0017
	pop bc
	ld hl,75
	push hl
	call __pchr
	pop bc
	ld hl,S5
	push hl
	call _Z0006
	pop bc
	ld l,(ix-16)
	ld h,(ix-15)
	push hl
	call _Z0017
	pop bc
	ld hl,S6
	push hl
	call _Z0006
	pop bc
	ld l,(ix-12)
	ld h,(ix-11)
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,(ix-10)
	ld h,(ix-9)
	or a
	sbc hl,de
	push hl
	call _Z0017
	pop bc
	ld hl,S7
	push hl
	call _Z0006
	pop bc
	ld l,(ix-14)
	ld h,(ix-13)
	push hl
	call _Z0017
	pop bc
	ld hl,S8
	push hl
	call _Z0006
	pop bc
	ld l,(ix-23)
	ld h,0
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld hl,65
	add hl,de
	push hl
	call __pchr
	pop bc
	ld hl,S9
	push hl
	call _Z0006
	pop bc
	ld hl,0
L222:
	ld sp,ix
	pop ix
	ret
;@dcc.mir phi-slot
;@dcc.lto end _main

;@dcc.lto begin _Z0001

; static function szsort
_Z0001:
	push ix
	ld ix,0
	add ix,sp
	ld hl,-4
	add hl,sp
	ld sp,hl
	ld a,(ix+6) ; peep: zeroext_byte_cmp_biased_const
	cp 2
	jr nc,L3 ; peep: jp_to_jr
	ld sp,ix
	pop ix
	ret
L3:
	ld l,(ix+6)
	ld h,0
	ld de,1
	or a
	sbc hl,de
	ld a,l
	ld (_Z0002),a
L5:
	ld a,(_Z0002)
	ld l,a
	ld h,0
	ld (ix-2),l
	ld (ix-1),h
	ld a,h
	or l
	jp z, L6
	ld l,(ix+4)
	ld h,(ix+5)
	ld (_Z0003),hl
	ld a,(_Z0002)
	ld l,a
	ld a,l
	ld (_Z0004),a
L8:
	ld hl,(_Z0003)
	ld c,l ; peep: global_word_cache_store
	ld b,h
	ld de,11
	add hl,de
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	push hl
	ld l,c ; peep: global_word_cache_load
	ld h,b
	ld de,24 ; peep: combine_hl_constant_adds
	add hl,de
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	push hl
	pop de
	pop hl
	or a
	sbc hl,de
	jr nc,L11 ; peep: jp_to_jr
	push bc ; peep: aggregate_swap_ldir
	pop de
	ld hl,13
	add hl,de
	ex de,hl
	ld b,13
Laswap_479:
	ld c,(hl)
	ld a,(de)
	ld (hl),a
	ld a,c
	ld (de),a
	inc hl
	inc de
	djnz Laswap_479
L11:
	ld a,(_Z0003)
	add a,13
	ld (_Z0003),a
	jr nc,L16 ; peep: jp_to_jr
	ld a,(_Z0003+1)
	inc a
	ld (_Z0003+1),a
L16:
	ld a,(_Z0004)
	ld l,a
	ld h,0
	dec hl ; peep: sbc_de1_to_dec
	ld (ix-2),l
	ld (ix-1),h
	ld a,l
	ld (_Z0004),a
	ld h,0
	ld a,h
	or l
	jp nz, L8 ; peep: branch_over_jump
	ld a,(_Z0002)
	ld l,a
	ld de,1 ; peep: push_lde_pop
	or a
	sbc hl,de
	ld a,l
	ld (_Z0002),a
	jp L5
L6:
	ld sp,ix
	pop ix
	ret
;@dcc.lto end _Z0001
;@dcc.lto begin _Z0006

; static function cputs
_Z0006:
	push ix
	ld ix,0
	add ix,sp
	dec sp ; peep: local_alloc_2
	dec sp
L19:
	ld l,(ix+4)
	ld h,(ix+5)
	ld a,(hl)
	or a ; peep: cp0_or_a
	jr z,L20 ; peep: jp_to_jr
	ld l,(ix+4)
	ld h,(ix+5)
	ld (ix-2),l
	ld (ix-1),h
	inc hl ; peep: ld_de1_to_inc
	ld (ix+4),l
	ld (ix+5),h
	ld l,(ix-2)
	ld h,(ix-1)
	ld l,(hl)
	ld a,l
	rlca
	sbc a,a
	ld h,0
	push hl
	call __pchr
	pop bc
	jp L19
L20:
	ld sp,ix
	pop ix
	ret
;@dcc.lto end _Z0006
;@dcc.lto begin _Z0007

; static function count_bits
_Z0007:
	push ix
	ld ix,0
	add ix,sp
	ld hl,-5
	add hl,sp
	ld sp,hl
	ld l,(ix+4)
	ld h,(ix+5)
	ld (ix-3),l
	ld (ix-2),h
	ld hl,0
	ld (ix-1),l
L24:
	ld a,(ix-2) ; peep: ix_word_zero_test_via_mem
	or (ix-3)
	jr z,L25 ; peep: jp_to_jr
	ld l,(ix-3)
	ld h,(ix-2)
	ld h,0
	ld a,l
	and 1
	ld l,a
	ld (ix-5),l
	ld (ix-4),h
	ld l,(ix-1)
	ld e,(ix-5) ; peep: ix_pair_load_to_de
	ld d,(ix-4)
	add hl,de
	ld h,0
	ld (ix-5),l
	ld (ix-4),h
	ld l,(ix-3)
	ld h,(ix-2)
	ld de,1
	srl h
	rr l
	ld (ix-3),l
	ld (ix-2),h
	ld l,(ix-5)
	ld h,(ix-4)
	ld (ix-1),l
	jp L24
L25:
	ld l,(ix-1)
	ld h,0
	ld sp,ix
	pop ix
	ret
;@dcc.mir phi-slot
;@dcc.lto end _Z0007
;@dcc.lto begin _Z0008

; static function count_free_blocks
_Z0008:
	push ix
	ld ix,0
	add ix,sp
	ld hl,-12
	add hl,sp
	ld sp,hl
	ld l,(ix+6)
	ld h,(ix+7)
	ld (ix-6),l
	ld (ix-5),h
	ld (ix-2),0 ; peep: ix_zero_store_before_hl_overwrite
	ld (ix-1),0
	ld l,(ix+4)
	ld h,(ix+5)
	ld (ix-8),l
	ld (ix-7),h
	inc hl ; peep: ld_de1_to_inc
	ld (ix+4),l
	ld (ix+5),h
	ld l,(ix-8)
	ld h,(ix-7)
	ld l,(hl)
	ld (ix-3),l
	ld (ix-4),128
L30:
	ld l,(ix-6)
	ld h,(ix-5)
	ld de,1
	or a
	sbc hl,de
	ld (ix-8),l
	ld (ix-7),h
	ld a,(ix-5) ; peep: ix_word_zero_test_via_mem
	or (ix-6)
	jp z, L31
	ld l,(ix+8)
	ld h,0
	ld (ix-10),l
	ld (ix-9),h
	ld a,h
	or l
	jr z,L33 ; peep: jp_to_jr
	ld l,(ix+8)
	ld de,1 ; peep: push_lde_pop
	or a
	sbc hl,de
	ld (ix+8),l
	jr L34 ; peep: jp_to_jr
L33:
	ld l,(ix-3)
	ld h,0
	ld (ix-10),l
	ld (ix-9),h
	ld l,(ix-4)
	ld (ix-12),l
	ld (ix-11),h
	ld l,(ix-10)
	ld h,(ix-9)
	ld (ix-10),l
	ld (ix-9),h
	ld l,(ix-12)
	ld h,(ix-11)
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,(ix-10)
	ld h,(ix-9)
	ld a,h
	and d
	ld h,a
	ld a,l
	and e
	ld l,a
	ld a,h
	or l
	jr nz,L34 ; peep: jp_to_jr
	inc (ix-2)
	jr nz,L34 ; peep: jp_to_jr
	inc (ix-1)
L34:
	ld l,(ix-4)
	ld h,0
	ld de,1 ; peep: push_lde_pop
	sra h
	rr l
	ld (ix-10),l
	ld (ix-9),h
	ld (ix-4),l
	ld a,h
	or l
	jr nz,L38 ; peep: jp_to_jr
	ld a,(ix-7) ; peep: ix_word_zero_test_via_mem
	or (ix-8)
	jr z,L38 ; peep: jp_to_jr
	ld l,(ix+4)
	ld h,(ix+5)
	ld (ix-10),l
	ld (ix-9),h
	inc hl ; peep: ld_de1_to_inc
	ld (ix+4),l
	ld (ix+5),h
	ld l,(ix-10)
	ld h,(ix-9)
	ld l,(hl)
	ld (ix-3),l
	ld (ix-4),128
L38:
	ld l,(ix-8)
	ld h,(ix-7)
	ld (ix-6),l
	ld (ix-5),h
	jp L30
L31:
	ld l,(ix-2)
	ld h,(ix-1)
	ld sp,ix
	pop ix
	ret
;@dcc.mir phi-slot
;@dcc.lto end _Z0008
;@dcc.lto begin _Z0009

; static function entry_block_count
_Z0009:
	push ix
	ld ix,0
	add ix,sp
	dec sp ; peep: local_alloc_wide
	dec sp ; peep: local_alloc_wide
	dec sp ; peep: local_alloc_wide
	dec sp ; peep: local_alloc_wide
	ld (ix-1),0
	ld hl,16
	ld (ix-2),l
	ld c,(ix+4) ; peep: hoist_index_ptr_to_bc
	ld b,(ix+5) ; peep: hoist_index_ptr_to_bc
L49:
	ld a,(ix-2) ; peep: zeroext_byte_cmp_biased_const
	cp 32
	jp nc, L50
	ld l,a ; peep: a_tracks_ix
	ld h,0
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,c ; peep: hoist_index_ptr_to_bc
	ld h,b ; peep: hoist_index_ptr_to_bc
	add hl,de
	ld a,(hl)
	or a ; peep: cp0_or_a
	jr nz,L65 ; peep: jp_to_jr
	ld l,(ix+6)
	ld a,l
	or a
	ld hl,0
	jr z,L66 ; peep: jp_to_jr
	inc hl
L66:
	ld a,h
	or l
	jr z,L52 ; peep: jp_to_jr
	ld l,(ix-2)
	ld h,0
	inc hl ; peep: ld_de1_to_inc
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,c ; peep: hoist_index_ptr_to_bc
	ld h,b ; peep: hoist_index_ptr_to_bc
	add hl,de
	ld a,(hl)
	or a ; peep: cp0_or_a
	jr z,L52 ; peep: jp_to_jr
L65:
	ld l,(ix-1)
	ld h,0
	inc hl ; peep: ld_de1_to_inc
	ld (ix-1),l
L52:
	ld l,(ix+6)
	ld a,l
	or a
	ld hl,0
	jr z,L68 ; peep: jp_to_jr
	inc hl
L68:
	ld h,0
	inc hl ; peep: ld_de1_to_inc
	ld (ix-4),l
	ld (ix-3),h
	ld l,(ix-2)
	ld h,0
	ld e,(ix-4) ; peep: ix_pair_load_to_de
	ld d,(ix-3)
	add hl,de
	ld h,0
	ld (ix-4),l
	ld (ix-3),h
	ld (ix-2),l
	jp L49
L50:
	ld l,(ix-1)
	ld h,0
	ld sp,ix
	pop ix
	ret
;@dcc.mir phi-slot
;@dcc.lto end _Z0009
;@dcc.lto begin _Z0010

; static function matches_pattern
_Z0010:
	push ix
	ld ix,0
	add ix,sp
	ld hl,-7
	add hl,sp
	ld sp,hl
	ld hl,93
	ld (ix-2),l
	ld (ix-1),h
	ld a,(hl)
	or a ; peep: cp0_or_a
	jr z,L89 ; peep: jp_to_jr
	ld l,(ix-2)
	ld h,(ix-1)
	ld a,(hl)
	cp 32
	jr nz,L71 ; peep: jp_to_jr
L89:
	ld hl,1
	jp L90
L71:
	ld hl,0
	ld (ix-3),l
	ld c,(ix-2) ; peep: hoist_index_ptr_to_bc
	ld b,(ix-1) ; peep: hoist_index_ptr_to_bc
L80:
	ld a,(ix-3) ; peep: zeroext_byte_cmp_biased_const
	cp 11
	jp nc, L81
	ld l,c ; peep: hoist_index_ptr_to_bc
	ld h,b ; peep: hoist_index_ptr_to_bc
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,a ; peep: a_tracks_ix
	ld h,0
	add hl,de
	ld a,(hl)
	cp 63
	jr z,L83 ; peep: jp_to_jr
	ld l,c ; peep: hoist_index_ptr_to_bc
	ld h,b ; peep: hoist_index_ptr_to_bc
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,(ix-3)
	ld h,0
	add hl,de
	ld l,(hl)
	ld h,0
	ld (ix-5),l
	ld (ix-4),h
	ld l,(ix-3)
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,(ix+4)
	ld h,(ix+5)
	add hl,de
	ld l,(hl)
	ld h,0
	ld a,l
	and 127
	ld l,a
	push bc ; peep: ix_spill_exx
	exx ; peep: ix_spill_exx
	ld l,(ix-5)
	ld h,(ix-4)
	exx ; peep: ix_spill_exx
	push hl ; peep: ix_spill_exx
	exx ; peep: ix_spill_exx
	pop de ; peep: ix_spill_exx
	pop bc ; peep: ix_spill_exx
	or a
	sbc hl,de
	jr z,L83 ; peep: jp_to_jr
	ld hl,0
	jr L90 ; peep: jp_to_jr
L83:
	ld l,(ix-3)
	ld h,0
	inc l ; peep: ld_de1_byte_update
	ld (ix-5),l
	ld (ix-4),h
	ld (ix-3),l
	jp L80
L81:
	ld hl,1
L90:
	ld sp,ix
	pop ix
	ret
;@dcc.mir phi-slot
;@dcc.lto end _Z0010
;@dcc.lto begin _Z0011

; static function insert_entry
_Z0011:
	push ix
	ld ix,0
	add ix,sp
	ld hl,-25
	add hl,sp
	ld sp,hl
	ld (ix-19),0 ; peep: ix_const_store_hl_dead
	ld (ix-18),0
	ld (ix-15),0
	ld hl,1
	ld (ix-21),l
	ld (ix-20),h
	ld (ix-17),l
	ld (ix-16),h
	ld hl,0
	ld (ix-14),l
	ld c,(ix+4) ; peep: hoist_index_ptr_to_bc
	ld b,(ix+5) ; peep: hoist_index_ptr_to_bc
L94:
	ld a,(ix-14) ; peep: zeroext_byte_cmp_biased_const
	cp 11
	jr nc,L95 ; peep: jp_to_jr
	ld l,a ; peep: a_tracks_ix
	ld h,0
	push hl
	push ix
	pop hl
	ld de,-13
	add hl,de
	pop de
	add hl,de
	push hl
	ld l,a ; peep: a_tracks_ix
	ld h,0
	inc hl ; peep: ld_de1_to_inc
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,c ; peep: hoist_index_ptr_to_bc
	ld h,b ; peep: hoist_index_ptr_to_bc
	add hl,de
	ld l,(hl)
	ld h,0
	ld a,l
	and 127
	ld l,a
	push hl
	pop de
	pop hl
	ld (hl),e
	ld l,(ix-14)
	ld h,0
	inc l ; peep: ld_de1_byte_update
	ld (ix-23),l
	ld (ix-22),h
	ld (ix-14),l
	jp L94
L95:
	ld l,(ix+6)
	ld a,l
	or a
	ld hl,0
	jr z,L109 ; peep: jp_to_jr
	inc hl
L109:
	push hl
	ld l,(ix+4)
	ld h,(ix+5)
	push hl
	call _Z0009
	pop bc
	pop bc
	push hl
	push ix
	pop hl
	ld de,-2 ; peep: combine_hl_constant_adds
	add hl,de
	pop de
	ld (hl),e
	inc hl
	ld (hl),d
	ld l,(ix-19)
	ld h,(ix-18)
	ld (ix-15),l
L97:
	ld a,(_Z0012)
	ld l,a
	ld h,0
	ld (ix-23),l
	ld (ix-22),h
	ld l,(ix-15)
	ld (ix-25),l
	ld (ix-24),h
	ld l,(ix-23)
	ld h,(ix-22)
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,(ix-25)
	ld h,(ix-24)
	ld a,l ; peep: fold_signed_cmp_via_bytes
	sub e
	ld a,h
	sbc a,d
	jp po,LS0
	xor 128
LS0:
	rlca
	jp nc, L98
	push ix
	pop hl
	ld de,-13
	add hl,de
	ld (ix-23),l
	ld (ix-22),h
	ld l,(ix-15)
	ld h,0
	ld d,h
	ld e,l
	add hl,hl
	add hl,de
	add hl,hl
	add hl,hl
	add hl,de
	ld de,_Z0013 ; peep: add_hl_immediate_direct_de
	add hl,de
	ld c,l
	ld b,h
	ld l,(ix-23)
	ld h,(ix-22)
	push hl
	push bc ; peep: global_word_cache_load_push_cached_bc
	ld hl,11
	ld b,h
	ld c,l
	pop hl
	pop de
	call __cmpf
	ld (ix-25),l
	ld (ix-24),h
	ld (ix-17),l
	ld (ix-16),h
	ld a,h
	or l
	jr z,L98 ; peep: jp_to_jr
	bit 7,h
	jr nz,L98 ; peep: jp_to_jr
	ld l,(ix-15)
	ld h,0
	inc l ; peep: ld_de1_byte_update
	ld (ix-23),l
	ld (ix-22),h
	ld (ix-15),l
	ld l,(ix-15)
	ld (ix-15),l
	jp L97
L98:
	ld a,(ix-16) ; peep: ix_word_zero_test_via_mem
	or (ix-17)
	jr nz,L104 ; peep: jp_to_jr
	ld l,(ix-15)
	ld h,0
	ld d,h
	ld e,l
	add hl,hl
	add hl,de
	add hl,hl
	add hl,hl
	add hl,de
	ld de,_Z0013 ; peep: add_hl_immediate_direct_de
	add hl,de
	ld de,11
	add hl,de
	ld (ix-19),l
	ld (ix-18),h
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	push hl
	ld l,(ix-2) ; peep: ix_offset_word_load_direct
	ld h,(ix-1)
	ld a,(ix-2)
	push hl
	pop de
	pop hl
	add hl,de
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,(ix-19)
	ld h,(ix-18)
	ld (hl),e
	inc hl
	ld (hl),d
	ld hl,1
	jp L111
L104:
	ld a,(_Z0012)
	ld l,a
	ld h,0
	ld (ix-19),l
	ld (ix-18),h
	ld de,64
	or a
	sbc hl,de
	jr nz,L107 ; peep: jp_to_jr
	ld hl,0
	jp L111
L107:
	ld l,(ix-15)
	ld h,0
	inc hl ; peep: ld_de1_to_inc
	ld d,h
	ld e,l
	add hl,hl
	add hl,de
	add hl,hl
	add hl,hl
	add hl,de
	ld de,_Z0013 ; peep: add_hl_immediate_direct_de
	add hl,de
	ld (ix-19),l
	ld (ix-18),h
	ld l,(ix-15)
	ld h,0
	ld d,h
	ld e,l
	add hl,hl
	add hl,de
	add hl,hl
	add hl,hl
	add hl,de
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld hl,_Z0013
	add hl,de
	ld (ix-21),l
	ld (ix-20),h
	ld a,(_Z0012)
	ld l,a
	ld h,0
	ld (ix-23),l
	ld (ix-22),h
	ld l,(ix-15)
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,(ix-23)
	ld h,(ix-22)
	or a
	sbc hl,de
	ld (ix-23),l
	ld (ix-22),h
	ld d,h
	ld e,l
	add hl,hl
	add hl,de
	add hl,hl
	add hl,hl
	add hl,de
	push hl
	ld l,(ix-21)
	ld h,(ix-20)
	push hl
	ld l,(ix-19)
	ld h,(ix-18)
	push hl
	call __mmov
	pop bc
	pop bc
	pop bc
	ld l,(ix-15)
	ld h,0
	ld d,h
	ld e,l
	add hl,hl
	add hl,de
	add hl,hl
	add hl,hl
	add hl,de
	ld de,_Z0013 ; peep: add_hl_immediate_direct_de
	add hl,de
	ld (ix-19),l
	ld (ix-18),h
	push hl
	push ix
	pop hl
	ld de,-13
	add hl,de
	pop de
	ld bc,13
	ldir
	ld a,(_Z0012)
	ld l,a
	ld h,0
	inc hl ; peep: ld_de1_to_inc
	ld a,l
	ld (_Z0012),a
	ld hl,1
L111:
	ld sp,ix
	pop ix
	ret
;@dcc.mir phi-slot
;@dcc.lto end _Z0011
;@dcc.lto begin _Z0014

; static function read_directory
_Z0014:
	push ix
	ld ix,0
	add ix,sp
	push iy
	ld hl,-11
	add hl,sp
	ld sp,hl
;@dcc.reg claim=iy scope=function sym=read_directory kind=mir val=0
	ld hl,_Z0015
	ld (ix-4),l
	ld (ix-3),h
	ld (ix-6),128 ; peep: ix_const_store_hl_dead
	ld (ix-5),0
	ld hl,0
	ld (ix-9),l
	ld c,(ix-4) ; peep: hoist_index_ptr_to_bc
	ld b,(ix-3) ; peep: hoist_index_ptr_to_bc
L114:
	ld a,(ix-9) ; peep: zeroext_byte_cmp_biased_const
	cp 36
	jr nc,L115 ; peep: jp_to_jr
	ld l,c ; peep: hoist_index_ptr_to_bc
	ld h,b ; peep: hoist_index_ptr_to_bc
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,a ; peep: a_tracks_ix
	ld h,0
	add hl,de
	ld (hl),0
	ld l,a ; peep: a_tracks_ix
	ld h,0
	inc l ; peep: ld_de1_byte_update
	ld (ix-11),l
	ld (ix-10),h
	ld (ix-9),l
	jp L114
L115:
	ld l,(ix-4)
	ld h,(ix-3)
	ld (hl),63
	ld hl,17
	ld e,(ix-4) ; peep: ix_pair_load_to_de
	ld d,(ix-3)
	ld c,l
	call __bdosf
	ld de,255 ; peep: push_lde_pop
	ld a,h
	and d
	ld h,a
	ld a,l
	and e
	ld l,a
	ld (ix-11),l
	ld (ix-10),h
	push hl
	pop iy
L117:
	push iy
	pop hl
	ld de,255
	or a
	sbc hl,de
	jp z, L118
	ld l,(ix-6)
	ld h,(ix-5)
	push hl
	push iy
	pop hl
	ld h,0
	ld a,l
	and 3
	ld l,a
	add hl,hl
	add hl,hl
	add hl,hl
	add hl,hl
	add hl,hl
	push hl
	pop de
	pop hl
	add hl,de
	ld (ix-8),l
	ld (ix-7),h
	ld a,(hl)
	cp 229
	jp z, L120
	ld l,(ix+8)
	ld h,(ix+9)
	inc (hl)
	jp nz,L132
	inc hl
	inc (hl)
L132:
	ld l,(ix-8)
	ld h,(ix-7)
	ld l,(hl)
	ld h,0
	push hl
	pop iy
	ld l,(ix+4)
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	push iy
	pop hl
	or a
	sbc hl,de
	jr nz,L120 ; peep: jp_to_jr
	ld l,(ix-8)
	ld h,(ix-7)
	inc hl ; peep: ld_de1_to_inc
	push hl
	call _Z0010
	pop bc
	ld a,h
	or l
	jr z,L120 ; peep: jp_to_jr
	ld l,(ix+6)
	ld a,l
	or a
	ld hl,0
	jr z,L134 ; peep: jp_to_jr
	inc hl
L134:
	push hl
	ld l,(ix-8)
	ld h,(ix-7)
	push hl
	call _Z0011
	pop bc
	pop bc
	ld a,h
	or l
	jr nz,L120 ; peep: jp_to_jr
	ld hl,0
	jr L136 ; peep: jp_to_jr
L120:
	ld hl,18
	ld e,(ix-4) ; peep: ix_pair_load_to_de
	ld d,(ix-3)
	ld c,l
	call __bdosf
	ld de,255 ; peep: push_lde_pop
	ld a,h
	and d
	ld h,a
	ld a,l
	and e
	ld l,a
	push hl
	pop iy
	jp L117
L118:
	ld hl,1
L136:
	ld sp,ix
	dec sp
	dec sp
	pop iy
	pop ix
	ret
;@dcc.mir phi-slot
;@dcc.lto end _Z0014
;@dcc.lto begin _Z0017

; static function put_number
_Z0017:
	push ix
	ld ix,0
	add ix,sp
	ld hl,-5
	add hl,sp
	ld sp,hl
	ld (ix-1),0
	ld hl,0
	ld (ix-3),l
L139:
	ld a,(ix-3) ; peep: zeroext_byte_cmp_biased_const
	cp 5
	jp nc, L140
	ld (ix-2),0
L142:
	ld l,(ix+4)
	ld h,(ix+5)
	push hl
	ld l,(ix-3)
	ld h,0
	add hl,hl
	ld de,_Z0018 ; peep: add_hl_immediate_direct_de
	add hl,de
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	push hl
	pop de
	pop hl
	or a
	sbc hl,de
	jr c,L143 ; peep: jp_to_jr
	ld l,(ix+4)
	ld h,(ix+5)
	push hl
	ld l,(ix-3)
	ld h,0
	add hl,hl
	ld de,_Z0018 ; peep: add_hl_immediate_direct_de
	add hl,de
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	push hl
	pop de
	pop hl
	or a
	sbc hl,de
	ld (ix+4),l
	ld (ix+5),h
	ld l,(ix-2)
	ld h,0
	inc hl ; peep: ld_de1_to_inc
	ld (ix-2),l
	jp L142
L143:
	ld l,(ix-2)
	ld h,0
	ld a,h
	or l
	jr nz,L166 ; peep: jp_to_jr
	ld l,(ix-1)
	ld a,l
	or a
	ld hl,0
	jr z,L165 ; peep: jp_to_jr
	inc hl
L165:
	ld a,h
	or l
	jr nz,L166 ; peep: jp_to_jr
	ld a,(ix-3) ; peep: byte_de_cmp
	cp 4
	jr nz,L146 ; peep: jp_to_jr
L166:
	ld l,(ix-2)
	ld h,0
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld hl,48
	add hl,de
	push hl
	call __pchr
	pop bc
	ld (ix-1),1
L146:
	ld l,(ix-3)
	ld h,0
	inc l ; peep: ld_de1_byte_update
	ld (ix-5),l
	ld (ix-4),h
	ld (ix-3),l
	jp L139
L140:
	ld sp,ix
	pop ix
	ret
;@dcc.mir phi-slot
;@dcc.lto end _Z0017
;@dcc.lto begin _Z0019

; static function put_size
_Z0019:
	push ix
	ld ix,0
	add ix,sp
	ld l,(ix+4)
	ld h,(ix+5)
	ld de,1000
	or a
	sbc hl,de
	jr nc,L169 ; peep: jp_to_jr
	ld hl,32
	push hl
	call __pchr
	pop bc
L169:
	ld l,(ix+4)
	ld h,(ix+5)
	ld de,100
	or a
	sbc hl,de
	jr nc,L171 ; peep: jp_to_jr
	ld hl,32
	push hl
	call __pchr
	pop bc
L171:
	ld l,(ix+4)
	ld h,(ix+5)
	ld de,10
	or a
	sbc hl,de
	jr nc,L173 ; peep: jp_to_jr
	ld hl,32
	push hl
	call __pchr
	pop bc
L173:
	ld l,(ix+4)
	ld h,(ix+5)
	push hl
	call _Z0017
	pop bc
	ld hl,75
	push hl
	call __pchr
	pop bc
	pop ix
	ret
;@dcc.lto end _Z0019
;@dcc.lto begin _Z0020

; static function put_file
_Z0020:
	push ix
	ld ix,0
	add ix,sp
	dec sp ; peep: local_alloc_wide
	dec sp ; peep: local_alloc_wide
	dec sp ; peep: local_alloc_wide
	ld hl,0
	ld (ix-1),l
L177:
	ld a,(ix-1) ; peep: zeroext_byte_cmp_biased_const
	cp 11
	jr nc,L178 ; peep: jp_to_jr
	ld l,(ix+4)
	ld h,(ix+5)
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,a ; peep: a_tracks_ix
	ld h,0
	add hl,de
	ld l,(hl)
	ld h,0
	push hl
	call __pchr
	pop bc
	ld l,(ix-1)
	ld h,0
	inc l ; peep: ld_de1_byte_update
	ld (ix-3),l
	ld (ix-2),h
	ld (ix-1),l
	jp L177
L178:
	ld hl,32
	push hl
	call __pchr
	pop bc
	ld l,(ix+4)
	ld h,(ix+5)
	ld de,11
	add hl,de
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	ld e,(ix+6) ; peep: ix_byte_load_to_de
	ld d,0
	ld b,e
	ld a,b
	or a
	jr z,L181 ; peep: jp_to_jr
L180:
	add hl,hl
	djnz L180
L181:
	push hl
	call _Z0019
	ld sp,ix
	pop ix
	ret
;@dcc.mir phi-slot
;@dcc.lto end _Z0020
;@dcc.lto begin _Z0021

; static function put_listing
_Z0021:
	push ix
	ld ix,0
	add ix,sp
	ld hl,-10
	add hl,sp
	ld sp,hl
	ld a,(_Z0012)
	ld l,a
	ld h,0
	ld de,4
	add hl,de
	ld de,1 ; peep: push_lde_pop
	or a
	sbc hl,de
	ld de,2 ; peep: push_lde_pop
	sra h
	rr l
	sra h
	rr l
	ld h,0
	ld (ix-6),l
	ld (ix-5),h
	ld hl,S2
	push hl
	call _Z0006
	pop bc
	ld hl,0
	ld (ix-2),l
L184:
	ld l,(ix-2)
	ld h,0
	ld (ix-8),l
	ld (ix-7),h
	ld l,(ix-6)
	ld h,(ix-5)
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,(ix-8)
	ld h,(ix-7)
	ld a,h
	xor 128
	ld h,a
	ld a,d
	xor 128
	ld d,a
	sbc hl,de
	jp nc, L185
	ld l,(ix-2)
	ld (ix-4),l
	ld (ix-3),0
L187:
	ld l,(ix-3)
	ld h,0
	ld (ix-8),l
	ld (ix-7),h
	ld de,32772
	ld a,h
	xor 128
	ld h,a
	sbc hl,de
	jp nc, L188
	ld l,(ix-4)
	ld h,0
	ld (ix-8),l
	ld (ix-7),h
	ld a,(_Z0012)
	ld l,a
	ld (ix-10),l
	ld (ix-9),h
	ld l,(ix-8)
	ld h,(ix-7)
	ld (ix-8),l
	ld (ix-7),h
	ld l,(ix-10)
	ld h,(ix-9)
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,(ix-8)
	ld h,(ix-7)
	ld a,l ; peep: fold_signed_cmp_via_bytes
	sub e
	ld a,h
	sbc a,d
	jp po,LS1
	xor 128
LS1:
	rlca
	jp nc, L190
	ld l,(ix-4)
	ld h,0
	ld d,h
	ld e,l
	add hl,hl
	add hl,de
	add hl,hl
	add hl,hl
	add hl,de
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld hl,_Z0013
	add hl,de
	ld c,l
	ld b,h
	ld l,(ix+4)
	ld h,0
	push hl
	push bc
	call _Z0020
	pop bc
	pop bc
	ld l,(ix-4)
	ld h,0
	ld (ix-8),l
	ld (ix-7),h
	ld l,(ix-6)
	ld h,(ix-5)
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,(ix-8)
	ld h,(ix-7)
	add hl,de
	push hl
	ld a,(_Z0012)
	ld l,a
	ld h,0
	push hl
	pop de
	pop hl
	ld a,h
	xor 128
	ld h,a
	ld a,d
	xor 128
	ld d,a
	sbc hl,de
	jr nc,L190 ; peep: jp_to_jr
	ld hl,S0
	push hl
	call _Z0006
	pop bc
L190:
	ld l,(ix-4)
	ld h,0
	ld (ix-8),l
	ld (ix-7),h
	ld l,(ix-6)
	ld h,(ix-5)
	ex de,hl ; peep: push_hl_load_pop_de_to_ex
	ld l,(ix-8)
	ld h,(ix-7)
	add hl,de
	ld h,0
	ld (ix-4),l
	ld l,(ix-3)
	inc hl ; peep: ld_de1_to_inc
	ld (ix-3),l
	jp L187
L188:
	ld hl,S1
	push hl
	call _Z0006
	pop bc
	ld l,(ix-2)
	ld h,0
	inc l ; peep: ld_de1_byte_update
	ld (ix-8),l
	ld (ix-7),h
	ld (ix-2),l
	jp L184
L185:
	ld sp,ix
	pop ix
	ret
;@dcc.mir phi-slot
;@dcc.lto end _Z0021

; Validate the raw command tail before directory searches overwrite the DMA.
; C bits: 0=filespec seen, 1=-S seen, 2=-H seen. Carry means finish now.
LSARGS:
        ld a,(0080h)
        ld e,a
        ld hl,0081h
        ld c,0
LSNEXT:
        ld a,e
        or a
        jr z,LSFIN
        ld a,(hl)
        cp ' '
        jr z,LSSKIP
        cp 9
        jr nz,LSTRT
LSSKIP:
        inc hl
        dec e
        jr LSNEXT
LSTRT:
        push hl
        ld b,0
LSSCAN:
        ld a,(hl)
        cp ' '
        jr z,LSTEND
        cp 9
        jr z,LSTEND
        inc hl
        inc b
        dec e
        jr nz,LSSCAN
LSTEND:
        ex (sp),hl              ; token start in HL, end on stack
        push hl
        call LSTOK
        pop hl
        jr c,LSABAD
        pop hl
        jr LSNEXT
LSFIN:
        bit 2,c
        jr nz,LSHELP
        or a
        ret
LSABAD:
        pop de                  ; discard token end; HL/B retain bad token
        push hl
        push bc
        ld de,LSERR
        or a                    ; error class: 0=argument, 1=drive
        jr z,LSEPFX
        ld de,LSDMSG
LSEPFX:
        ld c,9
        call 5
        pop bc
        pop hl
LSECHO:
        push hl
        push bc
        ld e,(hl)
        ld c,2
        call 5
        pop bc
        pop hl
        inc hl
        djnz LSECHO
        ld de,LSHINT
        jr LSPRNT
LSHELP:
        ld de,LSUSAG
LSPRNT:
        ld c,9
        call 5
        scf
        ret
LSTOK:
        bit 2,c
        jr nz,LSTBAD
        ld a,(hl)
        cp '-'
        jr nz,LSFILE
        ld a,b
        cp 2
        jr nz,LSTBAD
        inc hl
        ld a,(hl)
        and 0dfh
        cp 'H'
        jr z,LSHOPT
        cp 'S'
        jr nz,LSTBAD
        bit 1,c
        jr nz,LSTBAD
        set 1,c
        ld a,1
        ld (_Z0023),a
        bit 0,c
        jr nz,LSTOKY
        ; With -S first (or alone), the second CCP FCB holds the filespec.
        push bc
        push de
        ld hl,006ch
        ld de,005ch
        ld bc,12
        ldir
        pop de
        pop bc
LSTOKY:
        or a
        ret
LSHOPT:
        ld a,c
        or a
        jr nz,LSTBAD
        set 2,c
        ret
LSFILE:
        bit 0,c
        jr nz,LSTBAD
        push bc
        call LSFSP
        pop bc
        ret c
        set 0,c
        or a
        ret
LSTBAD:
        xor a                   ; argument error
        scf
        ret
LSDBAD:
        ld a,1                  ; drive error
        scf
        ret

; Validate one CP/M 8.3 filespec, with an optional BIOS-supported drive A-P.
; A star must end its name/extension field. The CCP supplies the actual FCB.
LSFSP:
        ld a,b
        cp 2
        jr c,LSFNAM
        inc hl
        ld a,(hl)
        dec hl
        cp ':'
        jr nz,LSFNAM
        ld a,(hl)
        and 0dfh
        cp 'A'
        jr c,LSDBAD
        cp 'Q'
        jr nc,LSDBAD
        call LSDRV
        jr z,LSDBAD
        inc hl
        inc hl
        dec b
        dec b
        ld a,b
        or a
        ret z                   ; drive alone is valid
LSFNAM:
        ld a,(hl)
        cp '.'
        jr z,LSTBAD
        ld c,0                  ; bit 0=extension, bit 1=star ends this field
        ld d,8
LSFCHR:
        ld a,(hl)
        inc hl
        dec b
        cp '.'
        jr z,LSFDOT
        bit 1,c
        jr nz,LSTBAD
        cp '*'
        jr z,LSFSTR
        call LSFCHK
        ret c
        jr LSFDEC
LSFSTR:
        set 1,c
LSFDEC:
        ld a,d
        or a
        jr z,LSTBAD
        dec d
        jr LSFNXT
LSFDOT:
        bit 0,c
        jr nz,LSTBAD
        set 0,c
        res 1,c
        ld d,3
LSFNXT:
        ld a,b
        or a
        jr nz,LSFCHR
        ret
LSFCHK:
        cp 33
        jr c,LSTBAD
        cp 127
        jr nc,LSTBAD
        push hl
        push de
        ld e,a
        ld hl,LSBCHR
LSFCLP:
        ld a,(hl)
        or a
        jr z,LSFCOK
        cp e
        jr z,LSFCNO
        inc hl
        jr LSFCLP
LSFCNO:
        xor a                   ; argument error
        scf
LSFCOK:
        pop de
        pop hl
        ret
LSBCHR:
        db 34,60,62,44,59,58,61,91,93,47,92,124,0
LSERR:
        db 'LS: invalid argument: $'
LSDMSG:
        db 'LS: invalid drive: $'
LSHINT:
        db 13,10,'Use LS -H for help.',13,10,'$'
LSUSAG:
        db 'Usage: LS [-S] [filespec]',13,10
        db '       LS -H',13,10
        db ' -S  Sort largest first.',13,10
        db ' -H  Show help.',13,10
        db ' Filespec: [A-P:]NAME.EXT, * or ?.',13,10
        db ' -S may precede or follow filespec.',13,10,'$'

; A=drive letter. BIOS SELDSK returns HL=0 for an unsupported drive.
; Probe before BDOS 14 so a Select error cannot trigger a warm boot.
; SELDSK changes BIOS state: restore the BDOS current drive before returning.
; Preserve command-tail registers. Return Z for unavailable, NZ for valid.
LSDRV:
        push bc
        push de
        push hl
        sub 'A'
        push af
        ld c,25
        call 5
        ld c,a
        pop af
        push bc                 ; original drive in low byte
        ld c,a
        ld e,0                  ; first selection of the requested drive
        call LSBSEL
        ex (sp),hl              ; save requested DPH, recover original drive
        ld c,l
        ld e,1                  ; original drive is already logged in
        call LSBSEL
        pop hl
        ld a,h
        or l
        pop hl
        pop de
        pop bc
        ret
LSBSEL:
        ld hl,(0001h)           ; BIOS WBOOT jump entry = BIOS base + 3
        push de
        ld de,24                ; SELDSK = BIOS base + 27
        add hl,de
        pop de
        jp (hl)

; Local helper conventions match the listing routines above.
; BDOS byte result: C=function, DE=argument, HL=zero-extended A.
__bdosf:
        call 5
        ld l,a
        ld h,0
        ret
; BDOS pointer result: preserve the HL returned by BDOS.
__bhlf:
        jp 5

; Character argument is the word at SP+2. Caller removes that argument.
; Emit the byte directly: listing strings already contain CR/LF pairs.
__pchr:
        ld hl,2
        add hl,sp
        ld e,(hl)
        push de
        ld c,2
        call 5
        pop hl
        ld h,0
        ret

; memcmp: DE=left, HL=right, BC=count. Return signed difference in HL.
__cmpf:
        ld a,b
        or c
        jr z,LSCEQ
LSCMPL:
        ld a,(de)
        cp (hl)
        jr nz,LSCDIF
        inc de
        inc hl
        dec bc
        ld a,b
        or c
        jr nz,LSCMPL
LSCEQ:
        ld hl,0
        ret
LSCDIF:
        sub (hl)
        ld l,a
        sbc a,a
        ld h,a
        ret

; memmove: stack words at SP+2 = destination, source, byte count.
; Copy backwards when destination follows source, to handle overlapping rows.
__mmov:
        ld hl,2
        add hl,sp
        ld c,(hl)
        inc hl
        ld b,(hl)
        inc hl
        ld e,(hl)
        inc hl
        ld d,(hl)
        inc hl
        ld a,(hl)
        inc hl
        ld h,(hl)
        ld l,a
        push bc
        push hl
        ex de,hl
        pop bc
        pop de
        push de
        ld a,b
        or c
        jr z,LSMDON
        ld a,d
        cp h
        jr c,LSMFWD
        jr nz,LSMBCK
        ld a,e
        cp l
        jr c,LSMFWD
        jr z,LSMDON
LSMBCK:
        add hl,bc
        ex de,hl
        add hl,bc
        ex de,hl
        dec hl
        dec de
        lddr
        jr LSMDON
LSMFWD:
        ldir
LSMDON:
        pop hl
        ret

; string literals
S0:
	db 32,124,32,0
S1:
	db 13,10,0
S2:
	db 27,91,57,52,109,78,97,109,101,32,32,32,32,69,120,116,32,66,121,116,101,115,32,32,32,78,97
	db 109,101,32,32,32,32,69,120,116,32,66,121,116,101,115,32,32,32,78,97,109,101,32,32,32,32,69
	db 120,116,32,66,121,116,101,115,32,32,32,78,97,109,101,32,32,32,32,69,120,116,32,66,121,116
	db 101,115,27,91,48,109,13,10,0
S3:
	db 76,83,58,32,116,111,111,32,109,97,110,121,32,102,105,108,101,115,13,10,0
S4:
	db 32,70,105,108,101,40,115,41,44,32,111,99,99,117,112,121,105,110,103,32,0
S5:
	db 32,111,102,32,0
S6:
	db 75,32,116,111,116,97,108,32,99,97,112,97,99,105,116,121,13,10,0
S7:
	db 32,100,105,114,101,99,116,111,114,121,32,101,110,116,114,105,101,115,32,97,110,100,32,0
S8:
	db 75,32,98,121,116,101,115,32,114,101,109,97,105,110,32,111,110,32,0
S9:
	db 58,13,10,0

; initialized globals
;@dcc.lto begin _Z0018
_Z0018:
	dw 10000
	dw 1000
	dw 100
	dw 10
	dw 1
;@dcc.lto end _Z0018

	public	__stack_size
	__stack_size	equ	512

	public	__data_end
__data_end:
	public	__bssb
__bssb:
	_Z0005	equ	__bssb+0
	_Z0003	equ	__bssb+13
	_Z0002	equ	__bssb+15
	_Z0004	equ	__bssb+16
	_Z0013	equ	__bssb+17
	_Z0012	equ	__bssb+849
	_Z0015	equ	__bssb+850
	_Z0023	equ	__bssb+886
	public	__bssn
	__bssn	equ	887
	__bsse	equ	__bssb+887
	__hstart	equ	__bsse
	public	__bsse
	public	__hstart

LSSTK equ __bsse+512

	end
