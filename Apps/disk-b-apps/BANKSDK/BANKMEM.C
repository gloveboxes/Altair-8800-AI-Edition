/* DCC banked shared pool, ABI 1.0: resident BDOS, loader and extent
 * allocator occupy high bank-1 memory. General bmalloc allocations use
 * C800h..DDFFh (5,632 bytes) in 256-byte units.
 */
#include "BANKMEM.H"
#include "BIOSBANK.H"

/* Capability is stable for this program's lifetime; avoid a trapped IN for
 * every chunk. Heap metadata itself remains entirely in bank 1. The flag is
 * public (six-character bmrdy) so the transfer adapter can test it directly. */
#define bank_ready bmrdy
unsigned char bank_ready;
static bank_bios_info_t capabilities;
/* Call is a direct BIOS alias, so all memory APIs share status. */
extern unsigned int bmres;

unsigned int bank_memory_last_error(void)
{
    return bmres;
}

int bank_memory_initialize(void)
{
    bank_ready = 0;
    bmres = BANK_BIOS_UNAVAILABLE;
    if (memcmp((void *)BMBIOS_SIGNATURE, "BMX1", 4) ||
        *(unsigned char *)BMBIOS_VERSION != BMBIOS_ABI_MAJOR ||
        *(unsigned char *)BMBIOS_MINOR < BMBIOS_ABI_MINOR)
        return 0;
    if (bank_bios_version() != BMBIOS_ABI)
        return 0;
    if (!bank_bios_query(&capabilities))
        return 0;
    if (capabilities.bytes != 48 || capabilities.revision != 1 ||
        capabilities.abi != BMBIOS_ABI ||
        (capabilities.features & BANK_BIOS_FEATURES) != BANK_BIOS_FEATURES ||
        capabilities.allocator_revision != BMBIOS_CALL_REVISION ||
        capabilities.checked_read != BMBIOS_CHECKED_READ ||
        capabilities.checked_write != BMBIOS_CHECKED_WRITE ||
        !capabilities.allocation_bytes ||
        (capabilities.allocation_bytes & (capabilities.allocation_bytes - 1U)))
    {
        bmres = BANK_BIOS_UNAVAILABLE;
        return 0;
    }
    bank_ready = 1;
    bmres = BANK_BIOS_OK;
    return 1;
}

int bank_memory_reset(void)
{
    if (!bank_memory_initialize())
        return 0;
    return bank_bios_reset();
}

bank_memory_handle_t bank_memory_allocate(unsigned int bytes)
{
    unsigned int handle;
    if (!bank_ready && !bank_memory_initialize())
        return 0;
    if (!bytes || bytes > capabilities.pool_bytes)
    {
        bmres = BANK_BIOS_ARGUMENT;
        return 0;
    }
    handle = bank_bios_allocate(bytes);
    return handle;
}

unsigned int bank_memory_size(bank_memory_handle_t handle)
{
    unsigned int bytes;
    if (!bank_ready && !bank_memory_initialize())
        return 0;
    if (!handle || (handle & (capabilities.allocation_bytes - 1U)))
    {
        bmres = BANK_BIOS_HANDLE;
        return 0;
    }
    bytes = bank_bios_allocation_size(handle);
    return bytes;
}

unsigned int bank_memory_available(void)
{
    if (!bank_ready && !bank_memory_initialize())
        return 0;
    {
        unsigned int bytes = bank_bios_available();
        return bytes;
    }
}

int bank_memory_free(bank_memory_handle_t handle)
{
    if (!handle)
    {
        bmres = BANK_BIOS_OK;
        return 1;
    }
    if (!bank_ready && !bank_memory_initialize())
        return 0;
    return bank_bios_free(handle);
}

/* The BIOS checked entries look up the live extent on every call, so no
 * cached bounds can survive free/reset. A valid handle is 256-byte aligned:
 * its page selects the extent and handle+offset is the bank-1 address.
 * Stack on entry to _bxfer: ret, handle, offset, buffer, bytes; A=direction. */
#asm
;@dcc.lto begin _bread
	public _bread
_bread:
	xor a
	jp _bxfer
;@dcc.lto end _bread
;@dcc.lto begin _bwrite
	public _bwrite
_bwrite:
	ld a,1
	jp _bxfer
;@dcc.lto end _bwrite
;@dcc.lto begin _bxfer
	public _bxfer
_bxfer:
	push af
	ld a,(_bmrdy)
	or a
	jr nz,BX_READY
	call _binit
	ld a,h
	or l
	jr z,BX_DONE
BX_READY:
	ld hl,4
	add hl,sp
	ld a,(hl)
	or a
	jr nz,BX_HANDLE
	inc hl
	ld b,(hl)
	inc hl
	ld e,(hl)
	inc hl
	ld a,(hl)
	add a,b
	jr c,BX_ARGUMENT
	ld d,a
	ld a,b
	ex af,af'
	inc hl
	ld c,(hl)
	inc hl
	ld b,(hl)
	inc hl
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	push hl
	ld h,b
	ld l,c
	pop bc
	pop af
	or a
	jr nz,BX_WRITE
	ex af,af'
	call 0f806h
	jr BX_RESULT
BX_WRITE:
	ex af,af'
	call 0f809h
BX_RESULT:
	ld l,a
	ld h,0
	ld (_bmres),hl
	ld l,h
	ret c
	inc l
	ret
BX_HANDLE:
	ld hl,3
	jr BX_STATUS
BX_ARGUMENT:
	ld hl,2
BX_STATUS:
	ld (_bmres),hl
	ld hl,0
BX_DONE:
	pop af
	ret
;@dcc.lto end _bxfer
#endasm
