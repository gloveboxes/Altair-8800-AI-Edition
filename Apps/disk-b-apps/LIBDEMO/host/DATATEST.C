#include <stdio.h>
#include <string.h>

#include "API.H"
#include "BANKLIB.H"
#include "HOSTCHK.H"

static unsigned int previous_handle;

static unsigned int check_values(void)
{
    unsigned int left = 120, right = 23, result;
    result = add16(left, right);
    if (result != 143 || bank_library_last_error() != BL_OK || left != 120 || right != 23)
        return 0;
    printf("add16(120, 23) = %u; caller values unchanged.\n", result);
    return add16(65535U, 1) == 0 && bank_library_last_error() == BL_OK;
}

static unsigned int check_text(void)
{
    unsigned char text[128];
    unsigned int i;
    memset(text, 0xA5, sizeof(text));
    memcpy(text, "Bank 1", 7);
    if (upper(text, 6) != 6 || bank_library_last_error() != BL_OK ||
        memcmp(text, "BANK 1", 7))
        return 0;
    for (i = 7; i < sizeof(text); ++i)
        if (text[i] != 0xA5)
            return 0;
    printf("upper(\"Bank 1\") = \"%s\".\n", text);
    memset(text, 'a', sizeof(text));
    if (upper(text, sizeof(text)) != sizeof(text) || bank_library_last_error() != BL_OK)
        return 0;
    for (i = 0; i < sizeof(text); ++i)
        if (text[i] != 'A')
            return 0;
    if (upper(text, 129) != 0 || bank_library_last_error() != BL_BOUNDS)
        return 0;
    for (i = 0; i < sizeof(text); ++i)
        if (text[i] != 'A')
            return 0;
    return upper(0, 1) == 0 && bank_library_last_error() == BL_ARGUMENT;
}

static unsigned int check_file(void)
{
    unsigned char data[130], first;
    unsigned int i, checksum = 0;
    memset(data, 0xA5, sizeof(data));
    if (filerd(data + 1, 128) != 128 || bank_library_last_error() != BL_OK ||
        data[0] != 0xA5 || data[129] != 0xA5)
        return 0;
    first = data[1];
    for (i = 1; i <= 128; ++i)
        checksum += data[i];
    printf("Read 128 bytes from A:AUTORUN.COM in bank 1; byte sum %u.\n",
           checksum);
    return filerd(data + 1, 1) == 1 && bank_library_last_error() == BL_OK &&
           data[1] == first && data[0] == 0xA5 && data[129] == 0xA5;
}

static unsigned int check_exports(void)
{
    unsigned int i, j, found;
    unsigned char frame[19];
    char name[9], signature[BL_SIGNATURE_MAX + 1];
    if (bank_library_export_count() != 8)
        return 0;
    found = 0;
    for (i = 0; i < bank_library_export_count(); ++i)
    {
        if (!bank_library_export_name(i, name) || !bank_library_export_signature(i, signature, sizeof(signature)))
            return 0;
        printf("Export %s: %s\n", name, signature);
        if (!strcmp(name, "ADD16"))
        {
            found = 1;
            memset(frame, 0xA5, sizeof(frame));
            if (bank_library_call(name, "void()", frame, sizeof(frame)) ||
                bank_library_last_error() != BL_SIGNATURE)
                return 0;
            if (bank_library_call(name, signature, frame, 1) ||
                bank_library_last_error() != BL_DISPATCH)
                return 0;
            if (bank_library_call(name, signature, frame, BL_FRAME_MAX + 1) ||
                bank_library_last_error() != BL_BOUNDS)
                return 0;
            for (j = 0; j < sizeof(frame); ++j)
                if (frame[j] != 0xA5)
                    return 0;
        }
        if (!strcmp(name, "BLKFREE"))
        {
            char *field = strstr(signature, "state:i8@8");
            if (!field)
                return 0;
            memset(frame, 0xA5, sizeof(frame));
            *field = 'x';
            if (bank_library_call(name, signature, frame, sizeof(frame)) ||
                bank_library_last_error() != BL_SIGNATURE)
                return 0;
            *field = 's';
            if (bank_library_call(name, signature, frame, sizeof(frame) - 1) ||
                bank_library_last_error() != BL_DISPATCH)
                return 0;
            for (j = 0; j < sizeof(frame); ++j)
                if (frame[j] != 0xA5)
                    return 0;
        }
    }
    return found;
}

static unsigned int create_block(unsigned int seed, struct BlockInfo *info)
{
    struct BlockSpec spec;
    unsigned char saved[sizeof(spec)];
    unsigned int handle;
    spec.seed = seed;
    memcpy(spec.label, "BANKDATA", sizeof(spec.label));
    memcpy(saved, &spec, sizeof(spec));
    handle = blknew(&spec, info);
    if (memcmp(saved, &spec, sizeof(spec)) ||
        bank_library_last_error() != BL_OK || !handle ||
        info->handle != handle || info->size != 4096 ||
        info->byte_sum != 522240UL || info->state != 1 ||
        memcmp(info->label, "BANKDATA", sizeof(info->label)))
        return 0;
    return handle;
}

static unsigned int check_blocks(void)
{
    unsigned int handle, replacement, i;
    unsigned char data[130];
    struct {
        unsigned char before;
        struct BlockInfo info;
        unsigned char after;
    } guarded;
    struct BlockInfo other;
    struct BlockSpec spec;
    memset(&guarded, 0xA5, sizeof(guarded));
    spec.seed = 91;
    memcpy(spec.label, "BANKDATA", sizeof(spec.label));
    if (blknew(0, &guarded.info) || bank_library_last_error() != BL_ARGUMENT ||
        blknew(&spec, 0) || bank_library_last_error() != BL_ARGUMENT ||
        blknew((const struct BlockSpec *)&guarded.info, &guarded.info) ||
        bank_library_last_error() != BL_ARGUMENT ||
        blkfree(0) || bank_library_last_error() != BL_ARGUMENT)
        return 0;
    for (i = 0; i < sizeof(guarded); ++i)
        if (((unsigned char *)&guarded)[i] != 0xA5)
            return 0;
    handle = create_block(37, &guarded.info);
    if (!handle || bank_library_last_error() != BL_OK)
        return 0;
    memset(&other, 0xA5, sizeof(other));
    if (blknew(&spec, &other) != 0 || bank_library_last_error() != BL_OK)
        return 0;
    for (i = 0; i < sizeof(other); ++i)
        if (((unsigned char *)&other)[i] != 0)
            return 0;
    memset(data, 0xA5, sizeof(data));
    if (blkread(handle, 1023, data + 1, 128) != 128 ||
        bank_library_last_error() != BL_OK || data[0] != 0xA5 || data[129] != 0xA5)
        return 0;
    for (i = 0; i < 128; ++i)
        if (data[i + 1] != (unsigned char)(37 + 1023 + i))
            return 0;
    puts("Read a 128-byte slice of a 4096-byte bank-owned block.");
    memset(data, 0xA5, sizeof(data));
    if (blkread(handle, 0, data + 1, 129) || bank_library_last_error() != BL_BOUNDS)
        return 0;
    for (i = 0; i < sizeof(data); ++i)
        if (data[i] != 0xA5)
            return 0;
    if (blkread(handle, 4095, data + 1, 2) != 0 || bank_library_last_error() != BL_OK ||
        blkread(handle, 4096, data + 1, 1) != 0 || bank_library_last_error() != BL_OK ||
        blkread(handle, 65535U, data + 1, 2) != 0 || bank_library_last_error() != BL_OK ||
        blkread(0, 0, data + 1, 1) != 0 || bank_library_last_error() != BL_OK ||
        !blkfree(&guarded.info) || bank_library_last_error() != BL_OK ||
        blkread(handle, 0, data + 1, 1) != 0 || bank_library_last_error() != BL_OK ||
        blkfree(&guarded.info) != 0 || bank_library_last_error() != BL_OK ||
        guarded.info.handle || guarded.info.size || guarded.info.byte_sum ||
        guarded.info.state != -1 ||
        memcmp(guarded.info.label, "BANKDATA", sizeof(guarded.info.label)) ||
        guarded.before != 0xA5 || guarded.after != 0xA5)
        return 0;
    puts("PASS: flat record in/out/inout, 32-bit values, arrays and guards.");
    replacement = create_block(91, &guarded.info);
    if (!replacement || replacement == handle || bank_library_last_error() != BL_OK ||
        blkread(handle, 0, data + 1, 1) != 0 || bank_library_last_error() != BL_OK ||
        blkread(replacement, 0, data + 1, 1) != 1 || bank_library_last_error() != BL_OK ||
        data[1] != 91)
        return 0;
    /* Unloading, rather than blkfree, must retire this live handle. */
    previous_handle = replacement;
    return 1;
}

static unsigned int check_reload(void)
{
    unsigned int handle, result, cleanup;
    unsigned char data[1];
    struct BlockInfo info;
    if (!hsopen())
        return 0;
    handle = create_block(12, &info);
    result = handle && handle != previous_handle && bank_library_last_error() == BL_OK;
    if (result)
        result = blkread(previous_handle, 0, data, 1) == 0 &&
                 bank_library_last_error() == BL_OK &&
                 blkread(handle, 4095, data, 1) == 1 &&
                 bank_library_last_error() == BL_OK && data[0] == (unsigned char)(12 + 4095);
    /* Leave this block open to exercise the unload hook's cleanup. */
    cleanup = hsclos();
    return result && cleanup;
}

int main(void)
{
    unsigned int step, loaded;
    puts("DATATEST: ordinary C calls, separate banks.");
    puts("[1/8] Load DATA.LIB and run onload");
    loaded = hsopen();
    step = 1;
    if (!loaded)
        goto failed;
    puts("PASS: load and lifecycle.");

    step = 2;
    puts("[2/8] Integer arguments and results");
    if (!check_values())
        goto failed;
    puts("PASS: scalar values and 16-bit wraparound.");

    step = 3;
    puts("[3/8] Counted text buffer (inout)");
    if (!check_text())
        goto failed;
    puts("PASS: text copy-back, limits and null-pointer rejection.");

    step = 4;
    puts("[4/8] File I/O in bank 1 (out)");
    if (!check_file())
        goto failed;
    puts("PASS: file read, reopen and buffer guards.");

    step = 5;
    puts("[5/8] Export discovery and rejected calls");
    if (!check_exports())
        goto failed;
    puts("PASS: eight exports, signatures and malformed-frame rejection.");

    step = 6;
    puts("[6/8] Flat records, bank-owned blocks and opaque handles");
    if (!check_blocks())
        goto failed;
    puts("PASS: chunk data, bounds, stale handles and double-free rejection.");

    step = 7;
    puts("[7/8] Unload and check shared runtime state");
    loaded = 0;
    if (!hsclos())
        goto failed;
    puts("PASS: directory search preserved and transfer memory recovered.");

    step = 8;
    puts("[8/8] Reload, reject old handles and clean up an open block");
    if (!check_reload())
        goto failed;
    puts("PASS: reload isolation and unload cleanup.");
    puts("PASS: all 8 bank-1 library checks.");
    return 0;

failed:
    printf("FAIL: step %u (transport error %u).\n", step, bank_library_last_error());
    if (loaded && !hsclos())
        puts("FAIL: library cleanup.");
    return 1;
}
