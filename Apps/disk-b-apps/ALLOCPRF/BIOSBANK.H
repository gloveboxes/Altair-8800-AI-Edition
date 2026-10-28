#ifndef BIOSBANK_H
#define BIOSBANK_H
/* Public, platform-independent Z80 BIOS bank extension ABI 2.3.
 * Register calls: HL=base pointer, DE=bank offset, BC=byte count (multiple of
 * 16). Carry clear/A=0 succeeds; carry set/A=2 rejects invalid bounds/length.
 * INFO must succeed before READ/WRITE. Backend is responsible for banking.
 * The assembly adapter below translates dcc's ordinary stack convention.
 */
#define BMBIOS_HEAP 0xFF10 /* E=0 alloc,1 free,2 size,3 available,4 reset,5 call (BC=0 or stack handle) */
#define BMBIOS_MINOR 0xFF0E
#define BMBIOS_READ 0xFF00
#define BMBIOS_WRITE 0xFF03
#define BMBIOS_INFO 0xFF06
#define BMBIOS_SIGNATURE 0xFF09
#define BMBIOS_VERSION 0xFF0D
#define BMBIOS_ABI_MAJOR 2
#define BMBIOS_ABI_MINOR 3
#define BMBIOS_CALL_REVISION_OFFSET 8
#define BMBIOS_CALL_REVISION 3
#define BMBIOS_BURST_BYTES 16
#define BMBIOS_HEAP_START 0x0800
#define BMBIOS_HEAP_END 0xFF00
#define BMBIOS_HEAP_BYTES 63232U
/* INFO: version word (major<<8 | minor), or zero if unavailable. */
unsigned int bmstat(void);
/* ALLOC requires ABI major 2 and successful INFO; returns handle or zero.
 * Short symbol avoids the linker's six-character name collision with bmalloc. */
unsigned int bmaloc(unsigned int bytes);
unsigned int bmsize(unsigned int handle);
unsigned int bmavl(void);
int bmrset(void);
int bmfree(unsigned int handle); /* ABI 2.3 HEAP: E=1; HL=handle -> 1/0 */
/* These raw calls require successful INFO first; return 1 success / 0 error. */
int bmread(unsigned int bank, void *base, unsigned int bytes);
int bmwrit(unsigned int bank, const void *base, unsigned int bytes);
#endif
