/* dcc banked heap, ABI 2.3: extent metadata/code/private stack in 0000..07FF.
 * All heap operations execute in bank 1; no base C stack or BDOS runs there.
 * Application payloads occupy 0800..FEFF (63,232 bytes), in 256-byte units.
 */
#include <string.h>
#include "BANKMEM.H"
#include "BIOSBANK.H"

/* Capability is stable for this program's lifetime; avoid a trapped IN for
 * every chunk. Heap metadata itself remains entirely in bank 1. */
static unsigned char bank_ready;

/* Calls only public BIOS services; workspace layout belongs to the BIOS. */
static int bursts(unsigned int bank, void *base, unsigned int bytes, int writing)
{
    if (writing)
        return bmwrit(bank, base, bytes);
    return bmread(bank, base, bytes);
}

static int copy_bank(unsigned int address, void *buffer, unsigned int bytes,
                     int writing)
{
    unsigned char tail[BMBIOS_BURST_BYTES];
    unsigned char *cursor = buffer;
    unsigned int bulk, start, skip;
    unsigned int pointer = (unsigned int)buffer;
    if (!bank_ready && !binit())
        return 0;
    if (address > BMBIOS_HEAP_END ||
        bytes > BMBIOS_HEAP_END - address ||
        (bytes && (pointer < 256 || pointer >= BMBIOS_HEAP_END ||
                   bytes > BMBIOS_HEAP_END - pointer)))
        return 0;
    if (!bytes)
        return 1;
    if (!(bytes & (BMBIOS_BURST_BYTES - 1U)))
        return bursts(address, buffer, bytes, writing);
    bulk = bytes & ~(BMBIOS_BURST_BYTES - 1U);
    if (bulk)
    {
        if (!bursts(address, cursor, bulk, writing))
            return 0;
        address = address + bulk;
        cursor = cursor + bulk;
        bytes = bytes - bulk;
    }
    if (bytes)
    {
        start = address;
        if (start > BMBIOS_HEAP_END - BMBIOS_BURST_BYTES)
            start = BMBIOS_HEAP_END - BMBIOS_BURST_BYTES;
        skip = address - start;
        if (!bursts(start, tail, BMBIOS_BURST_BYTES, 0))
            return 0;
        if (writing)
        {
            memcpy(tail + skip, cursor, bytes);
            if (!bursts(start, tail, BMBIOS_BURST_BYTES, 1))
                return 0;
        }
        else
            memcpy(cursor, tail + skip, bytes);
    }
    return 1;
}

int binit(void)
{
    unsigned char header[8];
    bank_ready = 0;
    if (memcmp((void *)BMBIOS_SIGNATURE, "BMX2", 4) ||
        *(unsigned char *)BMBIOS_VERSION != BMBIOS_ABI_MAJOR ||
        (bmstat() >> 8) != BMBIOS_ABI_MAJOR)
        return 0;
    bank_ready = 1;
    if (!copy_bank(0, header, sizeof(header), 0) || memcmp(header, "BKM3BEX1", 8))
    {
        bank_ready = 0;
        return 0;
    }
    return 1;
}

int breset(void)
{
    if (!binit())
        return 0;
    return bmrset();
}

bhandle bmalloc(unsigned int bytes)
{
    if (!bytes || bytes > BMBIOS_HEAP_BYTES || (!bank_ready && !binit()))
        return 0;
    return bmaloc(bytes);
}

unsigned int bsize(bhandle handle)
{
    if (!handle || (handle & 255) || (!bank_ready && !binit()))
        return 0;
    return bmsize(handle);
}

unsigned int bavail(void)
{
    if (!bank_ready && !binit())
        return 0;
    return bmavl();
}

int bfree(bhandle handle)
{
    if (!handle)
        return 1;
    if (!bank_ready && !binit())
        return 0;
    return bmfree(handle);
}

static int transfer(bhandle handle, unsigned int offset, void *buffer,
                    unsigned int bytes, int writing)
{
    unsigned int capacity = bsize(handle);
    if (!capacity || offset > capacity || bytes > capacity - offset)
        return 0;
    /* The public BIOS checks base/bank ranges and alignment itself. Avoid
     * the C tail-buffer path when the complete request is already aligned. */
    if (!(bytes & (BMBIOS_BURST_BYTES - 1U)))
    {
        if (writing)
            return bmwrit(handle + offset, buffer, bytes);
        return bmread(handle + offset, buffer, bytes);
    }
    return copy_bank(handle + offset, buffer, bytes, writing);
}

int bread(bhandle handle, unsigned int offset, void *buffer, unsigned int bytes)
{
    return transfer(handle, offset, buffer, bytes, 0);
}

int bwrite(bhandle handle, unsigned int offset, const void *buffer, unsigned int bytes)
{
    return transfer(handle, offset, (void *)buffer, bytes, 1);
}
