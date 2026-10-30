/* Generated from interface.json. Do not edit. */
#include <string.h>
#include "../PRIMEBK1/BIOSBANK.H"
#include "../PRIMEBK1/BANKMEM.H"
#include "SPAPI.H"
#define LIB_ACTION 0xE4D0
#define LIB_FCB 0xE500
#define LIB_EXPORTS 0xE300
#define LIB_CODE 0x0100
#define LIB_STACK 0xC000

/* clang-format off */
#ifndef __INTELLISENSE__
#asm
        public _libop
_libop:
        ld e,8
        jp 0ff10h
#endasm
#endif
/* clang-format on */
unsigned int libop(void);
static unsigned char exports[80];
static unsigned char count;
static unsigned char ready;
static int invalidate_module(void);

static unsigned int word_at(const unsigned char *bytes)
{
    return bytes[0] | ((unsigned int)bytes[1] << 8);
}

/* Each export is an eight-byte name followed by a two-byte entry offset.
 * Resolve by name so a linker/export-order change cannot redirect a call. */
static unsigned int find_entry(const char *name)
{
    unsigned int i;
    unsigned char key[8];
    memset(key, 0, sizeof(key));
    for (i = 0; i < 8 && name[i]; ++i)
        key[i] = name[i];
    for (i = 0; i < count; ++i)
        if (!memcmp(exports + i * 10, key, 8))
            return word_at(exports + i * 10 + 8);
    return 0xffff;
}

static unsigned int call_entry(const char *name, unsigned int context)
{
    unsigned int offset;
    if (!ready || (offset = find_entry(name)) == 0xffff)
        return 0;
    return bmcall(LIB_CODE, LIB_STACK, offset, context);
}

static int load_module(void)
{
    unsigned char fcb[48], mailbox[16];
    memset(fcb, 0, sizeof(fcb));
    memcpy(fcb + 1, "SPREADS LIB", 11);
    memset(mailbox, 0, sizeof(mailbox));
    if (bmstat() != 0x0205 ||
        !bmwrit(LIB_FCB, fcb, sizeof(fcb)) ||
        !bmwrit(LIB_ACTION, mailbox, sizeof(mailbox)) || libop() != 0 ||
        !bmread(LIB_ACTION, mailbox, sizeof(mailbox)) ||
        mailbox[1] != 0 || mailbox[2] != 4 ||
        !bmread(LIB_EXPORTS, exports, sizeof(exports)))
        return 0;
    count = mailbox[2];
    ready = 1;
    if (find_entry("_ONLOAD") != 0xffff && find_entry("_VEG") != 0xffff && find_entry("_MARM") != 0xffff && find_entry("_UNLOAD") != 0xffff)
        return 1;
    invalidate_module();
    return 0;
}

static int invalidate_module(void)
{
    unsigned char mailbox[16];
    memset(mailbox, 0, sizeof(mailbox));
    mailbox[0] = 1;
    ready = 0;
    count = 0;
    return bmwrit(LIB_ACTION, mailbox, sizeof(mailbox)) && libop() == 0;
}

/* The two-byte argument is copied to a separate bank-1 allocation. */
static unsigned int call_inout(const char *name, unsigned int *value)
{
    unsigned int handle, result;
    unsigned char block[16];
    if (!ready || !value)
        return 0;
    handle = bmaloc(sizeof(block));
    if (!handle)
        return 0;
    memset(block, 0, sizeof(block));
    block[0] = (unsigned char)*value;
    block[1] = (unsigned char)(*value >> 8);
    result = 0;
    if (bmwrit(handle, block, sizeof(block)))
    {
        result = call_entry(name, handle);
        if (bmread(handle, block, sizeof(block)))
            *value = word_at(block);
        else
            result = 0;
    }
    if (!bmfree(handle))
        result = 0;
    return result;
}

unsigned int spopen(void)
{
    unsigned int result;
    if (ready || !load_module())
        return 0;
    result = call_entry("_ONLOAD", 0);
    if (!result)
        invalidate_module();
    return result;
}

unsigned int veg(unsigned int *value)
{
    return call_inout("_VEG", value);
}

unsigned int marm(unsigned int *value)
{
    return call_inout("_MARM", value);
}

unsigned int spclos(void)
{
    unsigned int result;
    if (!ready)
        return 0;
    result = call_entry("_UNLOAD", 0);
    return invalidate_module() ? result : 0;
}
