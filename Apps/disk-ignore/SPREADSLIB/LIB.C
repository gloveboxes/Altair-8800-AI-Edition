#include <stdio.h>
#include <stdlib.h>
#include "LIBAPI.H"

/* Ordinary DCC C calls in the bank-1 fixed image. The pointer argument
 * names a bank-1 parameter block. Runtime calls use the bank-1 BDOS. */
static FILE *library_file;

unsigned int onload(unsigned int *unused)
{
    FILE *file = fopen("A:AUTORUN.COM", "rb");
    (void)unused;
    library_file = file;
    return library_file != 0;
}

unsigned int onunload(unsigned int *unused)
{
    (void)unused;
    if (library_file)
    {
        fclose(library_file);
        library_file = 0;
    }
    return 1;
}

unsigned int veg(unsigned int *value)
{
    char *scratch = malloc(16);
    if (!scratch)
        return 0;
    scratch[0] = 'V';
    scratch[1] = 'E';
    scratch[2] = 'G';
    scratch[3] = 0;
    puts(scratch);
    free(scratch);
    *value += 7;
    return *value;
}

unsigned int marm(unsigned int *value)
{
    if (!library_file || fgetc(library_file) == EOF)
        return 0;
    puts("BANK 1 file read OK");
    *value += 11;
    return *value;
}
