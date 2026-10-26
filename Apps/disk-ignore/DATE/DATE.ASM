; altair-z80 standalone
; DATE.COM: print the host date through ROMLESS-SVC/1 and return to the CCP.
; Assemble and link with DCC M80C/L80C (build-asm-app.ps1).

        cseg
        public DATEENT
DATEENT:
        ld (DATESP),sp
        ld sp,DATESTK

        ; Begin clock group 20h, one-byte request (operation 04h).
        in a,(2)
        or a
        jr nz,DATEFAIL
        ld a,32
        out (2),a
        in a,(2)
        cp 3
        jr nz,DATEFAIL
        ld a,1
        out (3),a
        xor a
        out (3),a
        ld a,4
        out (3),a

        ; Poll with a finite budget, matching the shared service client.
        ld d,100
DATEWAIT:
        ld bc,65535
DATEPOLL:
        in a,(2)
        cp 2
        jr z,DATEREAD
        cp 255
        jr z,DATEFAIL
        cp 1
        jr z,DATEEMPTY
        cp 4
        jr z,DATEFAIL
        dec bc
        ld a,b
        or c
        jr nz,DATEPOLL
        dec d
        jr nz,DATEWAIT
        jr DATEFAIL

DATEREAD:
        ; Reply is a little-endian byte count followed by counted text.
        in a,(4)
        ld l,a
        in a,(4)
        ld h,a
        ld a,h
        or a
        jr nz,DATEFAIL             ; output buffer is bounded to 79 bytes
        ld a,l
        cp 80
        jr nc,DATEFAIL
        ld b,l
        ld hl,DATETXT
        ld a,b
        or a
        jr z,DATEFIN
DATECOPY:
        in a,(4)
        ld (hl),a
        inc hl
        djnz DATECOPY
DATEFIN:
        xor a
        ld (hl),a
        xor a
        out (2),a                  ; release shared channel
        ld hl,DATETXT
DATEOUT:
        ld a,(hl)
        or a
        jr z,DATECR
        push hl
        ld e,a
        ld c,2
        call 5
        pop hl
        inc hl
        jr DATEOUT
DATECR:
        ld e,13
        ld c,2
        call 5
        ld e,10
        ld c,2
        call 5
        jr DATERET

DATEEMPTY:
        xor a
        out (2),a
        jr DATEFAILMSG
DATEFAIL:
        ld a,1                     ; reset shared-channel parser state
        out (2),a
DATEFAILMSG:
DATEERR:
        ld de,DATERMSG
        ld c,9
        call 5
DATERET:
        ld de,0080h
        ld c,26                    ; restore CCP default DMA
        call 5
        ld sp,(DATESP)
        ret

DATESP: dw 0
DATERMSG: db 'DATE: clock service unavailable',13,10,'$'
DATETXT: ds 80
DATESTK equ DATETXT+80+512
        end
