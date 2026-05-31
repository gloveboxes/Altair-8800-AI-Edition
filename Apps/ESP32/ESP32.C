/*
 * esp32.c - ESP32 Stats CLI Tool
 *
 * Prints system and network statistics from the emulator's
 * onboard sensors using I/O ports. Plain CLI output - no
 * screen layout, colors or cursor control.
 *
 * To compile with BDS C:
 * cc esp32
 * clink esp32
 *
 * BDS C 1.6 on CP/M
 *
 * BDS C constraints:
 *  - All symbols unique within first 7 characters
 *  - K&R style definitions only
 *  - No support for casts
 */

#include <stdio.h>

/* Global buffer */
char buffer[256];

/* Function prototypes */
int main();
int rdstr();
int fld();
int bios(), bdos(), inp(), outp();
int atol(), itol(), ldiv(), lmod(), ltoa();

/* Long integer helper variables */
char luptime[4];
char l3600[4], l60[4];
char lhours[4], lrem[4], lmins[4];
char bufnum[16];

int main()
{
    printf("ESP32 Stats - Altair 8800\n");
    printf("Onboard system and network status\n\n");

    printf("System\n");

    /* Get Hostname - Port 48, data 0 */
    outp(48, 0);
    rdstr(buffer, 255);
    fld("Hostname", buffer);

    /* Get WiFi IP Address - Port 48, data 1 */
    outp(48, 1);
    rdstr(buffer, 255);
    fld("WiFi IP", buffer);

    /* Get Device ID - Port 48, data 2 */
    outp(48, 2);
    rdstr(buffer, 255);
    fld("Device ID", buffer);

    /* Get Emulator Version - Port 70 */
    outp(70, 0);
    rdstr(buffer, 255);
    fld("Emulator", buffer);

    /* Get Uptime - Port 41 (returns seconds string) */
    outp(41, 1);
    rdstr(buffer, 255);
    printf("\nUptime\n");
    fld("Seconds", buffer);

    /* Parse uptime to long for calculation */
    atol(luptime, buffer);
    itol(l3600, 3600);
    itol(l60, 60);

    /* Calculate Hours: luptime / 3600 */
    ldiv(lhours, luptime, l3600);

    /* Calculate Remainder: luptime % 3600 */
    lmod(lrem, luptime, l3600);

    /* Calculate Minutes: remainder / 60 */
    ldiv(lmins, lrem, l60);

    ltoa(bufnum, lhours);
    printf("  Hours:mins      %s:", bufnum);

    /* Format minutes with leading zero if needed */
    ltoa(bufnum, lmins);
    if (bufnum[1] == 0) /* Single digit? */
    {
        printf("0");
    }
    printf("%s\n", bufnum);

    return 0;
}

/*
 * Read string data from port 200 until null character
 */
int rdstr(buf, max)
char* buf;
int max;
{
    int i, ch;

    i = 0;
    ch = inp(200);

    while (ch != 0 && i < max - 1)
    {
        buf[i] = ch;
        i++;
        ch = inp(200);
    }

    buf[i] = 0; /* Null terminate */
    return i;
}

/* Print a labelled field. */
int fld(lab, val)
char *lab;
char *val;
{
    printf("  %-14s  %s\n", lab, val);
    return 0;
}
