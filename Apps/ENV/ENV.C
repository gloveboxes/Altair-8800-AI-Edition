/* ============================================================
 * ENV - Environment Variable Manager for CP/M
 * ============================================================
 * Command parsing and storage are handled by the ESP32 emulator
 * through DXENV.C and port_drivers/environment_io.c.
 * ============================================================
 */

#include "stdio.h"

int e_init();
int e_exec();

main(argc, argv)
int argc;
char *argv[];
{
    int rc;

    rc = e_init();
    if (rc != 0) {
        printf("Error: Cannot init ENV storage\r\n");
        return 1;
    }

    rc = e_exec(argc, argv);
    return rc == 0 ? 0 : 1;
}
