#include <string.h>
#include "BIOSBANK.H"
#include "BANKLIB.H"

static unsigned char table[96];
static unsigned char request[BANK_LIBRARY_REQUEST_BYTES];
static bank_bios_info_t capabilities;
static unsigned char count;
static unsigned char ready;
static unsigned char closing;
static unsigned int image_bytes;
static unsigned int error_code;
static unsigned int serial;
static unsigned int generation;
static unsigned int pool_epoch;
static unsigned int workspace;

static unsigned int word_at(const unsigned char *p)
{
    return p[0] | ((unsigned int)p[1] << 8);
}

static void put_word(unsigned int offset, unsigned int value)
{
    request[offset] = (unsigned char)value;
    request[offset + 1] = (unsigned char)(value >> 8);
}

static void begin_request(unsigned int action)
{
    memset(request, 0, sizeof(request));
    put_word(0, sizeof(request));
    put_word(2, 1);
    put_word(4, action);
}

static unsigned int service(void)
{
    unsigned int status;
    if (bank_bios_library_control(capabilities.library_request, request))
        return 1;
    status = bank_bios_last_error();
    switch (status)
    {
    case BANK_BIOS_ARGUMENT:
    case BANK_BIOS_HANDLE: error_code = BL_ARGUMENT; break;
    case BANK_BIOS_MEMORY: error_code = BL_MEMORY; break;
    case BANK_BIOS_DISPATCH: error_code = BL_DISPATCH; break;
    case BANK_BIOS_FORMAT: error_code = BL_FORMAT; break;
    case BANK_BIOS_IO: error_code = BL_IO; break;
    case BANK_BIOS_STALE: error_code = BL_STALE; break;
    case BANK_BIOS_EXHAUSTED: error_code = BL_EXHAUSTED; break;
    default: error_code = BL_ABI; break;
    }
    return 0;
}

static unsigned int query(void)
{
    begin_request(BANK_LIBRARY_QUERY);
    return service();
}

static unsigned int live(void)
{
    if (!query())
        return 0;
    if (word_at(request + 30) != pool_epoch)
    {
        /* RESET may have reassigned the same address to another owner. */
        workspace = 0;
        error_code = BL_STALE;
        return 0;
    }
    if (!word_at(request + 16) || word_at(request + 8) != generation)
    {
        error_code = BL_STALE;
        return 0;
    }
    return 1;
}

static unsigned int release_workspace(void)
{
    if (!workspace)
        return 1;
    if (!query())
        return 0;
    if (word_at(request + 30) != pool_epoch)
    {
        workspace = 0;
        error_code = BL_STALE;
        return 0;
    }
    if (!bank_bios_free(workspace))
    {
        error_code = BL_CLEANUP;
        return 0;
    }
    workspace = 0;
    return 1;
}

static unsigned char upper(unsigned char c)
{
    return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
}

void bank_library_set_error(unsigned int error)
{
    error_code = error;
}

unsigned int bank_library_last_error(void)
{
    return error_code;
}

/* Callers may hold pool leases only while the library session is current. */
unsigned int bank_library_check(void)
{
    error_code = BL_OK;
    if (!ready || !workspace)
    {
        error_code = BL_ARGUMENT;
        return 0;
    }
    return live();
}

unsigned int bank_library_export_count(void)
{
    return ready ? count : 0;
}

static int find(const char *name)
{
    unsigned char key[8];
    unsigned int i, length;
    if (!name)
        return -1;
    length = strlen(name);
    if (!length || length > 8)
        return -1;
    memset(key, 0, sizeof(key));
    for (i = 0; i < length; ++i)
        key[i] = upper(name[i]);
    for (i = 0; i < count; ++i)
        if (!memcmp(key, table + i * 12, 8))
            return (int)i;
    return -1;
}

unsigned int bank_library_export_name(unsigned int index, char *name)
{
    error_code = BL_OK;
    if (!ready || !name || index >= count)
    {
        error_code = BL_ARGUMENT;
        return 0;
    }
    memcpy(name, table + index * 12, 8);
    name[8] = 0;
    return 1;
}

static unsigned int read_signature(unsigned int index, char *signature,
                                   unsigned int capacity)
{
    unsigned char block[16];
    unsigned int offset, position, amount, i;
    offset = word_at(table + index * 12 + 10);
    position = 0;
    while (offset < image_bytes && position < capacity)
    {
        amount = image_bytes - offset;
        if (amount > sizeof(block))
            amount = sizeof(block);
        /* Transfers are rounded to BIOS bursts; inspect only image bytes. */
        if (!bank_bios_read(capabilities.image_origin + offset, block, sizeof(block)))
        {
            error_code = BL_IO;
            return 0;
        }
        for (i = 0; i < amount && position < capacity; ++i)
        {
            signature[position++] = block[i];
            if (!block[i])
                return 1;
        }
        offset += amount;
    }
    error_code = BL_BOUNDS;
    return 0;
}

unsigned int bank_library_export_signature(unsigned int index, char *signature, unsigned int capacity)
{
    error_code = BL_OK;
    if (!ready || !signature || index >= count ||
        !capacity || capacity > BL_SIGNATURE_MAX + 1)
    {
        error_code = BL_ARGUMENT;
        return 0;
    }
    return read_signature(index, signature, capacity);
}

static unsigned int invalidate(void)
{
    unsigned int result, saved_error;
    ready = 0;
    count = 0;
    begin_request(BANK_LIBRARY_INVALIDATE);
    result = service();
    saved_error = error_code;
    if (!release_workspace())
        result = 0;
    if (!result)
        error_code = BL_CLEANUP;
    else
        error_code = saved_error;
    return result;
}

static unsigned int prepare(const char *signature, void *frame,
                            unsigned int bytes)
{
    error_code = BL_OK;
    if (!ready || !signature || !frame)
    {
        error_code = BL_ARGUMENT;
        return 0;
    }
    if (!workspace)
    {
        error_code = BL_STALE;
        return 0;
    }
    if (!bytes || bytes > BL_FRAME_MAX)
    {
        error_code = BL_BOUNDS;
        return 0;
    }
    if (serial >= 65534U && (!closing || serial == 65535U))
    {
        error_code = BL_EXHAUSTED;
        return 0;
    }
    return live();
}

static unsigned int resolve(const char *name, const char *signature)
{
    static char actual[BL_SIGNATURE_MAX + 1];
    int index = find(name);
    if (index < 0)
        error_code = BL_ARGUMENT;
    else if (!read_signature((unsigned int)index, actual, sizeof(actual)))
        return 0;
    else if (strcmp(actual, signature))
        error_code = BL_SIGNATURE;
    else
        return (unsigned int)index + 1;
    return 0;
}

static unsigned int invoke(unsigned int index, void *frame, unsigned int bytes)
{
    static unsigned char block[BL_FRAME_MAX + 16];
    unsigned int size = (bytes + 17U) & 0xFFF0U;
    ++serial;
    begin_request(BANK_LIBRARY_INVOKE);
    put_word(8, generation);
    put_word(10, index - 1);
    put_word(12, workspace);
    put_word(14, serial);
    put_word(30, pool_epoch);
    block[0] = (unsigned char)bytes;
    block[1] = (unsigned char)(bytes >> 8);
    memcpy(block + 2, frame, bytes);
    memset(block + bytes + 2, 0, size - bytes - 2);
    if (!bank_bios_write(workspace, block, size))
    {
        error_code = BL_IO;
        return 0;
    }
    if (!service())
        return 0;
    if (!bank_bios_read(workspace, block, size))
    {
        error_code = BL_IO;
        return 0;
    }
    memcpy(frame, block + 2, bytes);
    return 1;
}

unsigned int bank_library_call(const char *name, const char *signature,
                               void *frame, unsigned int bytes)
{
    unsigned int index;
    if (!prepare(signature, frame, bytes))
        return 0;
    index = resolve(name, signature);
    return index ? invoke(index, frame, bytes) : 0;
}

unsigned int bank_library_call_bound(bank_library_binding_t *binding,
                                     const char *name, const char *signature,
                                     void *frame, unsigned int bytes)
{
    if (!binding || !name)
    {
        error_code = BL_ARGUMENT;
        return 0;
    }
    if (!prepare(signature, frame, bytes))
        return 0;
    if (!binding->index || binding->generation != generation)
    {
        binding->index = resolve(name, signature);
        binding->generation = generation;
        if (!binding->index)
            return 0;
    }
    if (binding->index > count)
    {
        error_code = BL_ARGUMENT;
        return 0;
    }
    return invoke(binding->index, frame, bytes);
}

static unsigned int lifecycle(const char *name)
{
    unsigned char frame[2];
    if (find(name) < 0)
        return 1;
    memset(frame, 0, sizeof(frame));
    if (!bank_library_call(name, "u16()", frame, sizeof(frame)))
        return 0;
    if (word_at(frame) != 1)
    {
        error_code = BL_LIFECYCLE;
        return 0;
    }
    return 1;
}

static unsigned int make_fcb(const char *filename, unsigned char *fcb)
{
    unsigned int i, position, length;
    unsigned char c;
    if (!filename)
        return 0;
    memset(fcb, 0, 48);
    memset(fcb + 1, ' ', 11);
    position = 0;
    if (filename[0] && filename[1] == ':')
    {
        c = upper(filename[0]);
        if (c < 'A' || c > 'D')
            return 0;
        fcb[0] = c - 'A' + 1;
        position = 2;
    }
    length = 0;
    while ((c = upper(filename[position])) != 0 && c != '.')
    {
        if (length == 8 || !((c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_'))
            return 0;
        fcb[1 + length++] = c;
        ++position;
    }
    if (!length)
        return 0;
    if (c == '.')
    {
        ++position;
        for (i = 0; i < 3; ++i)
        {
            c = upper(filename[position]);
            if (c != (unsigned char)"LIB"[i])
                return 0;
            ++position;
        }
        if (filename[position])
            return 0;
    }
    memcpy(fcb + 9, "LIB", 3);
    return 1;
}

unsigned int bank_library_open(const char *filename)
{
    unsigned char fcb[48];
    unsigned int saved_error;
    error_code = BL_OK;
    if (ready || workspace || !make_fcb(filename, fcb))
    {
        error_code = BL_ARGUMENT;
        return 0;
    }
    if (bank_bios_version() != BMBIOS_ABI || !bank_bios_query(&capabilities) ||
        capabilities.bytes != 48 || capabilities.revision != 1 ||
        capabilities.abi != BMBIOS_ABI ||
        (capabilities.features & BANK_BIOS_FEATURES) != BANK_BIOS_FEATURES ||
        capabilities.allocator_revision != BMBIOS_CALL_REVISION ||
        capabilities.library_request_bytes != sizeof(request) ||
        capabilities.frame_bytes != BL_FRAME_MAX ||
        capabilities.signature_bytes != BL_SIGNATURE_MAX ||
        !capabilities.export_count || capabilities.export_count > 8)
    {
        error_code = BL_ABI;
        return 0;
    }
    if (!query())
        return 0;
    pool_epoch = word_at(request + 30);
    workspace = bank_bios_allocate(BL_FRAME_MAX + 16);
    if (!workspace)
    {
        error_code = BL_MEMORY;
        return 0;
    }
    if (!bank_bios_write(workspace, fcb, sizeof(fcb)))
    {
        error_code = BL_IO;
        saved_error = error_code;
        if (release_workspace())
            error_code = saved_error;
        else
            error_code = BL_CLEANUP;
        return 0;
    }
    begin_request(BANK_LIBRARY_LOAD);
    put_word(12, workspace);
    if (!service())
        goto failed;
    generation = word_at(request + 8);
    count = (unsigned char)word_at(request + 18);
    image_bytes = word_at(request + 22);
    if (!word_at(request + 16) || !count ||
        word_at(request + 18) > capabilities.export_count ||
        word_at(request + 20) != 12 ||
        !image_bytes)
        error_code = BL_FORMAT;
    else if (!bank_bios_read(word_at(request + 24), table, sizeof(table)))
        error_code = BL_IO;
    else
    {
        ready = 1;
        if (lifecycle("ONLOAD"))
            return 1;
    }
failed:
    saved_error = error_code;
    if (invalidate())
        error_code = saved_error;
    return 0;
}

unsigned int bank_library_close(void)
{
    unsigned int result, saved_error;
    error_code = BL_OK;
    if (!ready && !workspace)
    {
        error_code = BL_ARGUMENT;
        return 0;
    }
    if (!ready)
        return invalidate();
    if (!live())
    {
        saved_error = error_code;
        if (saved_error != BL_STALE)
            return 0;
        if (word_at(request + 8) == generation)
        {
            if (!invalidate())
                return 0;
        }
        else
        {
            ready = 0;
            count = 0;
            if (!release_workspace())
            {
                error_code = BL_CLEANUP;
                return 0;
            }
        }
        error_code = saved_error;
        return 0;
    }
    closing = 1;
    result = lifecycle("ONUNLOAD");
    closing = 0;
    saved_error = error_code;
    if (!invalidate())
        return 0;
    error_code = saved_error;
    return result;
}
