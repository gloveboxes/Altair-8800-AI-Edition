"""Generate bank-0 declarations and stubs from the bank-1 export contract."""
from pathlib import Path
import json

folder = Path(__file__).resolve().parent
spec = json.loads((folder / "interface.json").read_text())
exports = spec["exports"]
library_name = spec["library"]
assert library_name.isascii() and library_name.isalnum() and 1 <= len(library_name) <= 8
assert library_name == library_name.upper()
assert 1 <= len(exports) <= 8
assert len({e["name"] for e in exports}) == len(exports)
assert len({e["host"] for e in exports}) == len(exports)
for e in exports:
    assert len(e["name"]) <= 8 and e["name"].startswith("_")
    assert e["argument"] in ("none", "inout_u16")
    assert len(e["host"]) <= 6 and e["host"].isidentifier()
assert [e.get("lifecycle") for e in exports].count("load") == 1
assert [e.get("lifecycle") for e in exports].count("unload") == 1
assert all(e.get("lifecycle") in (None, "load", "unload") for e in exports)
assert all(e["argument"] == "none" for e in exports if e.get("lifecycle"))

header = ['/* Generated from interface.json. Do not edit. */',
          '#ifndef SPAPI_H', '#define SPAPI_H']
for e in exports:
    arg = 'void' if e['argument'] == 'none' else 'unsigned int *value'
    header.append(f'unsigned int {e["host"]}({arg});')
header += ['#endif', '']
(folder / 'SPAPI.H').write_text('\n'.join(header))

library_header = ['/* Generated from interface.json. Do not edit. */',
                  '#ifndef LIBAPI_H', '#define LIBAPI_H']
for e in exports:
    # CALL AT always supplies one bank-1 pointer, including lifecycle calls.
    function = e['symbol'].lstrip('_').lower()
    library_header.append(f'unsigned int {function}(unsigned int *context);')
library_header += ['#endif', '']
(folder / 'LIBAPI.H').write_text('\n'.join(library_header))

source = r'''/* Generated from interface.json. Do not edit. */
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
    memcpy(fcb + 1, "LIBRARY_NAME LIB", 11);
    memset(mailbox, 0, sizeof(mailbox));
    if (bmstat() != 0x0205 ||
        !bmwrit(LIB_FCB, fcb, sizeof(fcb)) ||
        !bmwrit(LIB_ACTION, mailbox, sizeof(mailbox)) || libop() != 0 ||
        !bmread(LIB_ACTION, mailbox, sizeof(mailbox)) ||
        mailbox[1] != 0 || mailbox[2] != EXPECTED_COUNT ||
        !bmread(LIB_EXPORTS, exports, sizeof(exports)))
        return 0;
    count = mailbox[2];
    ready = 1;
    if (VALIDATE_EXPORTS)
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
'''
source = source.replace('EXPECTED_COUNT', str(len(exports)))
source = source.replace('LIBRARY_NAME LIB', f'{library_name:<8}LIB')
source = source.replace('VALIDATE_EXPORTS', ' && '.join(f'find_entry("{e["name"]}") != 0xffff' for e in exports))
for e in exports:
    name, host = e['name'], e['host']
    lifecycle = e.get('lifecycle')
    if lifecycle == 'load':
        source += f'''\nunsigned int {host}(void)
{{
    unsigned int result;
    if (ready || !load_module())
        return 0;
    result = call_entry("{name}", 0);
    if (!result)
        invalidate_module();
    return result;
}}
'''
    elif lifecycle == 'unload':
        source += f'''\nunsigned int {host}(void)
{{
    unsigned int result;
    if (!ready)
        return 0;
    result = call_entry("{name}", 0);
    return invalidate_module() ? result : 0;
}}
'''
    elif e['argument'] == 'inout_u16':
        source += f'''\nunsigned int {host}(unsigned int *value)
{{
    return call_inout("{name}", value);
}}
'''
    else:
        source += f'''\nunsigned int {host}(void)
{{
    return call_entry("{name}", 0);
}}
'''
(folder / 'SPAPI.C').write_text(source)
print('generated SPAPI.H and SPAPI.C')
