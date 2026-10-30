/* DCC banked shared pool, ABI 2.6: resident BDOS, loader and extent
 * allocator occupy high bank-1 memory. General bmalloc allocations use
 * C800h..DDFFh (5,632 bytes) in 256-byte units.
 */
#include "BANKMEM.H"
#include "BIOSBANK.H"

/* Capability is stable for this program's lifetime; avoid a trapped IN for
 * every chunk. Heap metadata itself remains entirely in bank 1. */
static unsigned char bank_ready;
static bank_bios_info_t capabilities;
/* Execute/call are direct BIOS aliases, so all memory APIs share status. */
extern unsigned int bmres;

unsigned int bank_memory_last_error(void)
{
    return bmres;
}

int bank_memory_initialize(void)
{
    bank_ready = 0;
    bmres = BANK_BIOS_UNAVAILABLE;
    if (memcmp((void *)BMBIOS_SIGNATURE, "BMX2", 4) ||
        *(unsigned char *)BMBIOS_VERSION != BMBIOS_ABI_MAJOR ||
        *(unsigned char *)BMBIOS_MINOR < BMBIOS_ABI_MINOR)
        return 0;
    if (bank_bios_version() != 0x0206)
        return 0;
    if (!bank_bios_query(&capabilities))
        return 0;
    if (capabilities.bytes != 48 || capabilities.revision != 1 ||
        capabilities.abi != 0x0206 ||
        (capabilities.features & BANK_BIOS_FEATURES) != BANK_BIOS_FEATURES ||
        capabilities.allocator_revision != BMBIOS_CALL_REVISION ||
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

/* Keep the checked four-argument call in one DCC stack frame. Allocation size
 * is still queried on every call: no cached bounds can survive free/reset. */
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
	push ix
	ld ix,0
	add ix,sp
	push af
	ld l,(ix+4)
	ld h,(ix+5)
	push hl
	call _bsize
	pop bc
	ld a,h
	or l
	jr z,BX_FAILED
	ld c,(ix+6)
	ld b,(ix+7)
	or a
	sbc hl,bc
	jr c,BX_ARGUMENT
	ld c,(ix+10)
	ld b,(ix+11)
	or a
	sbc hl,bc
	jr c,BX_ARGUMENT
	ld l,(ix+4)
	ld h,(ix+5)
	ld e,(ix+6)
	ld d,(ix+7)
	add hl,de
	ex de,hl
	ld l,(ix+8)
	ld h,(ix+9)
	pop af
	or a
	jr nz,BX_WRITE
	call 0f800h
	jr BX_RESULT
BX_WRITE:
	call 0f803h
BX_RESULT:
	ld (_bmres),a
	ld hl,0
	jr c,BX_RETURN
	inc l
BX_RETURN:
	pop ix
	ret
BX_ARGUMENT:
	ld hl,2
	ld (_bmres),hl
BX_FAILED:
	pop af
	ld hl,0
	jr BX_RETURN
;@dcc.lto end _bxfer
#endasm
