#ifndef BIOSBANK_H
#define BIOSBANK_H
/* Public Z80 BIOS bank extension ABI 1.0.
 * Register calls: HL=base pointer, DE=bank offset, BC=byte count (multiple of
 * 16). Carry clear/A=0 succeeds; carry set/A=2 rejects invalid bounds/length.
 * INFO must succeed before READ/WRITE. Backend is responsible for banking.
 * The assembly adapter below translates dcc's ordinary stack convention.
 */
#define BMBIOS_HEAP 0xFF10 /* E=0..4 heap, 5 C CALL, 7 query, 8 library, 9 status */
#define BMBIOS_MINOR 0xFF0E
#define BMBIOS_READ 0xFF00
#define BMBIOS_WRITE 0xFF03
#define BMBIOS_INFO 0xFF06
#define BMBIOS_SIGNATURE 0xFF09
#define BMBIOS_VERSION 0xFF0D
#define BMBIOS_ABI_MAJOR 1
#define BMBIOS_ABI_MINOR 0
#define BMBIOS_ABI 0x0100
#define BMBIOS_CALL_REVISION_OFFSET 8
#define BMBIOS_CALL_REVISION 1
#define BMBIOS_BURST_BYTES 16
#define BMBIOS_HEAP_START 0xC800
#define BMBIOS_HEAP_END 0xFF00
#define BMBIOS_HEAP_BYTES 5632U
#define BANK_BIOS_OK 0
#define BANK_BIOS_UNAVAILABLE 1
#define BANK_BIOS_ARGUMENT 2
#define BANK_BIOS_HANDLE 3
#define BANK_BIOS_MEMORY 4
#define BANK_BIOS_PINNED 5
#define BANK_BIOS_DISPATCH 6
#define BANK_BIOS_FORMAT 7
#define BANK_BIOS_IO 8
#define BANK_BIOS_STALE 9
#define BANK_BIOS_EXHAUSTED 10
#define BANK_BIOS_FEATURES 31U
#define BANK_LIBRARY_REQUEST_BYTES 32
#define BANK_LIBRARY_LOAD 0
#define BANK_LIBRARY_INVALIDATE 1
#define BANK_LIBRARY_QUERY 2
#define BANK_LIBRARY_INVOKE 3

typedef struct {
    unsigned int bytes, revision, abi, features;
    unsigned int pool_start, pool_bytes, allocation_bytes, burst_bytes;
    unsigned int byte_read, byte_write, library_request, library_request_bytes;
    unsigned int exports_address, image_origin, code_handle, stack_handle;
    unsigned int token_address, frame_bytes, export_count, signature_bytes;
    unsigned int allocator_revision, reserved1, reserved2, reserved3;
} bank_bios_info_t;
/* Descriptive names map to six-character M80/L80 public symbols. */
#define bank_bios_version bmstat
#define bank_bios_allocate bmaloc
#define bank_bios_allocation_size bmsize
#define bank_bios_available bmavl
#define bank_bios_reset bmrset
#define bank_bios_free bmfree
#define bank_bios_read bmread
#define bank_bios_write bmwrit
#define bank_bios_call bmcall
#define bank_bios_query bmqury
#define bank_bios_library_control bmctrl
#define bank_bios_read_bytes bmrbyt
#define bank_bios_write_bytes bmwbyt
#define bank_bios_last_error bmerr

/* INFO: version word (major<<8 | minor), or zero if unavailable. */
unsigned int bank_bios_version(void);
unsigned int bank_bios_allocate(unsigned int bytes);
unsigned int bank_bios_allocation_size(unsigned int handle);
unsigned int bank_bios_available(void);
int bank_bios_reset(void);
int bank_bios_free(unsigned int handle);
/* These raw calls require successful INFO first; return 1 success / 0 error. */
int bank_bios_read(unsigned int bank_address, void *base, unsigned int bytes);
int bank_bios_write(unsigned int bank_address, const void *base, unsigned int bytes);
unsigned int bank_bios_call(unsigned int code_handle, unsigned int stack_handle,
                           unsigned int entry_offset, unsigned int context_handle);
int bank_bios_query(bank_bios_info_t *info);
int bank_bios_library_control(unsigned int request_address, void *request);
int bank_bios_read_bytes(unsigned int bank_address, void *base, unsigned int bytes);
int bank_bios_write_bytes(unsigned int bank_address, const void *base, unsigned int bytes);
unsigned int bank_bios_last_error(void);
#endif
