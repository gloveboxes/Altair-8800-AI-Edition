#include "intel8080.h"
#include "memory.h"
#include "universal_88dcdd.h"
#include "PortDrivers/chat_io.h"
#include "PortDrivers/environment_io.h"
#include "PortDrivers/host_files_io.h"
#include "PortDrivers/time_io.h"
#include "PortDrivers/weather_io.h"
#include "ansi_input.h"
#include "cpu_state.h"
#include "host_platform.h"
#include "io_ports.h"
#include "virtual_monitor.h"
#include "web_terminal.h"

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

static void wait_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts = {(time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
    nanosleep(&ts, NULL);
#endif
}

#define ASCII_MASK_7BIT 0x7f

/* Same byte the web terminal sends for Ctrl+M on the ESP firmware
   (see main/main.c). Distinct from CR so it can be routed through the
   normal terminal pipeline without losing Enter. On a host TTY this is
   produced by Ctrl+\. */
#define CPU_MONITOR_TOGGLE_CHAR 0x1c

#ifndef LOCAL_RUNNER_REPO_ROOT
#define LOCAL_RUNNER_REPO_ROOT ".."
#endif

#define DEFAULT_WEB_PORT 8080

static disk_controller_t g_disk_controller;
static bool g_disk_controller_ready = false;
static volatile sig_atomic_t keep_running = 1;
static bool g_web_mode = false;
static uint16_t g_web_port = DEFAULT_WEB_PORT;

static void terminal_write(uint8_t c);
static uint8_t sense_switches(void);

static const char *drive_a_path = LOCAL_RUNNER_REPO_ROOT "/disks/cpm63k.dsk";
static const char *drive_b_path = LOCAL_RUNNER_REPO_ROOT "/disks/bdsc-v1.60.dsk";
static const char *drive_c_path = LOCAL_RUNNER_REPO_ROOT "/disks/escape-posix.dsk";
static const char *drive_d_path = LOCAL_RUNNER_REPO_ROOT "/disks/blank.dsk";
static const char *apps_root_path = LOCAL_RUNNER_REPO_ROOT "/Apps";
static const char *env_file_path = LOCAL_RUNNER_REPO_ROOT "/altair_local/altair_env.txt";

/* CP/M 3 (CP/M Plus) profile disks. Selected with --cpm3 unless the matching
   drive was set explicitly on the command line. A: bootable 56K system disk,
   B: companion utilities/HELP disk. Both are DeRamp Mike Douglas Altair builds,
   downloaded from:
     https://deramp.com/downloads/altair/software/8_inch_floppy/CPM/CPM 3.0/
     (cpm3_v1.0_56K_disk1.dsk -> cpm3_56k_disk1.dsk,
      cpm3_v1.0_56k_disk2.dsk -> cpm3_56k_disk2.dsk)
   A pristine mirror is also kept in reference/cpm3_deramp/.
   All four cpm3 disks live in disk_archive/ (not disks/) so they are NOT
   bundled into the ESP32 storage-flash image, which packages everything in
   disks/. C: BDS C v1.60 compiler disk (standard Altair 8" data disk, read
   under CP/M 3). D: CP/M 2.2 system disk (cpm63k.dsk) with apps built under
   CP/M 2.2. */
static const char *cpm3_drive_a_path = LOCAL_RUNNER_REPO_ROOT "/disks_cpm_3/cpm3_56k_disk1.dsk";
static const char *cpm3_drive_b_path = LOCAL_RUNNER_REPO_ROOT "/disks_cpm_3/cpm3_56k_disk2.dsk";
static const char *cpm3_drive_c_path = LOCAL_RUNNER_REPO_ROOT "/disks_cpm_3/bdsc-v1.60.dsk";
static const char *cpm3_drive_d_path = LOCAL_RUNNER_REPO_ROOT "/disks_cpm_3/apps.dsk";

static void handle_signal(int signum)
{
    (void)signum;
    keep_running = 0;
}

static int read_raw_terminal_byte(void)
{
    if (g_web_mode)
    {
        uint8_t b;
        return web_terminal_rx_byte(&b) ? (int)b : -1;
    }
    return host_terminal_read_byte();
}

static uint8_t terminal_read(void)
{
    int raw_ch;
    uint8_t ch;

    raw_ch = read_raw_terminal_byte();
    if (raw_ch < 0)
    {
        return ansi_input_process(0x00, host_monotonic_ms());
    }

    ch = (uint8_t)raw_ch;
    ch &= ASCII_MASK_7BIT;
    if (ch == 0x1d)
    {
        keep_running = 0;
        return 0x00;
    }
    if (ch == CPU_MONITOR_TOGGLE_CHAR)
    {
        cpu_state_toggle_mode();
        return 0x00;
    }
    ch = ansi_input_process(ch, host_monotonic_ms());
    if (ch == '\n')
    {
        return '\r';
    }
    return ch;
}

void altair_reset(void)
{
    if (!g_disk_controller_ready)
    {
        return;
    }
    memset(memory, 0x00, 64 * 1024);
    loadDiskLoader(0xff00);
    i8080_reset(&cpu, terminal_read, terminal_write, sense_switches,
                &g_disk_controller, io_port_in, io_port_out);
    i8080_examine(&cpu, 0xff00);
    bus_switches = cpu.address_bus;
}

static void terminal_write(uint8_t c)
{
    unsigned char ch = (unsigned char)(c & ASCII_MASK_7BIT);

    if (g_web_mode)
    {
        web_terminal_tx_byte(ch);
        return;
    }

    if (!host_terminal_write_byte(ch))
    {
        keep_running = 0;
    }
}

static uint8_t sense_switches(void)
{
    return 0xff;
}

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--cpm3] [--web [PORT]] [--drive-a PATH] [--drive-b PATH] [--drive-c PATH] [--drive-d PATH] [--apps-root PATH] [--env-file PATH]\n"
            "\n"
            "  --cpm3   Boot CP/M 3 (CP/M Plus): A=cpm3_56k_disk1.dsk, B=cpm3_56k_disk2.dsk,\n"
            "           C=bdsc-v1.60.dsk, D=cpm63k.dsk (unless --drive-a/-b/-c/-d given).\n"
            "           Default boots CP/M 2.2.\n"
            "  --web [PORT]\n"
            "           Serve the browser terminal (terminal/index.html) and a WebSocket\n"
            "           bridge on PORT (default 8080) instead of using the stdio terminal.\n"
            "           Open http://localhost:PORT/ in a browser to connect.\n"
            "\n"
            "Defaults reference the repository disks and Apps folders:\n"
            "  A: %s\n"
            "  B: %s\n"
            "  C: %s\n"
            "  D: %s\n"
            "  Apps: %s\n"
            "  Env:  %s\n",
            program, drive_a_path, drive_b_path, drive_c_path, drive_d_path, apps_root_path, env_file_path);
}

static bool parse_args(int argc, char **argv)
{
    int i;
    bool use_cpm3 = false;
    bool drive_a_explicit = false;
    bool drive_b_explicit = false;
    bool drive_c_explicit = false;
    bool drive_d_explicit = false;

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--cpm3") == 0)
        {
            use_cpm3 = true;
        }
        else if (strcmp(argv[i], "--web") == 0)
        {
            g_web_mode = true;
            /* Optional port argument: only consume it when it looks numeric. */
            if (i + 1 < argc && argv[i + 1][0] != '-')
            {
                char *end = NULL;
                long p = strtol(argv[i + 1], &end, 10);
                if (end != NULL && *end == '\0' && p > 0 && p <= 65535)
                {
                    g_web_port = (uint16_t)p;
                    i++;
                }
                else
                {
                    fprintf(stderr, "altair-local: invalid --web port '%s'\n", argv[i + 1]);
                    return false;
                }
            }
        }
        else if (strcmp(argv[i], "--drive-a") == 0 && i + 1 < argc)
        {
            drive_a_path = argv[++i];
            drive_a_explicit = true;
        }
        else if (strcmp(argv[i], "--drive-b") == 0 && i + 1 < argc)
        {
            drive_b_path = argv[++i];
            drive_b_explicit = true;
        }
        else if (strcmp(argv[i], "--drive-c") == 0 && i + 1 < argc)
        {
            drive_c_path = argv[++i];
            drive_c_explicit = true;
        }
        else if (strcmp(argv[i], "--drive-d") == 0 && i + 1 < argc)
        {
            drive_d_path = argv[++i];
            drive_d_explicit = true;
        }
        else if (strcmp(argv[i], "--apps-root") == 0 && i + 1 < argc)
        {
            apps_root_path = argv[++i];
        }
        else if (strcmp(argv[i], "--env-file") == 0 && i + 1 < argc)
        {
            env_file_path = argv[++i];
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            print_usage(argv[0]);
            return false;
        }
        else
        {
            print_usage(argv[0]);
            return false;
        }
    }

    if (use_cpm3)
    {
        if (!drive_a_explicit)
        {
            drive_a_path = cpm3_drive_a_path;
        }
        if (!drive_b_explicit)
        {
            drive_b_path = cpm3_drive_b_path;
        }
        if (!drive_c_explicit)
        {
            drive_c_path = cpm3_drive_c_path;
        }
        if (!drive_d_explicit)
        {
            drive_d_path = cpm3_drive_d_path;
        }
    }

    return true;
}

int main(int argc, char **argv)
{
    if (!parse_args(argc, argv))
    {
        return argc > 1 ? 1 : 0;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    host_files_init(apps_root_path);
    environment_io_init(env_file_path);
    chat_io_init();
    weather_io_init();

    if (g_web_mode)
    {
        if (!web_terminal_start(LOCAL_RUNNER_REPO_ROOT, g_web_port))
        {
            fprintf(stderr, "altair-local: failed to start web terminal on port %u\n", g_web_port);
            return 1;
        }
        fprintf(stderr,
                "[altair-local] Web terminal ready: http://localhost:%u/\n"
                "[altair-local] Ctrl+] (in the browser) exits; Ctrl+C here stops the server.\n"
                "[altair-local] Waiting for a browser to connect...\n",
                g_web_port);
    }
    else
    {
        fprintf(stderr, "[altair-local] Ctrl+\\ toggles the CPU monitor, Ctrl+] exits.\n");

        if (!host_terminal_configure())
        {
            return 1;
        }
        atexit(host_terminal_restore);
    }
    host_prefer_efficiency_core();

    if (!host_disk_init(drive_a_path, drive_b_path, drive_c_path, drive_d_path))
    {
        if (g_web_mode)
        {
            web_terminal_stop();
        }
        else
        {
            host_terminal_restore();
        }
        fprintf(stderr, "altair-local: failed to open disk images\n");
        fprintf(stderr, "  A: %s\n  B: %s\n  C: %s\n  D: %s\n", drive_a_path, drive_b_path, drive_c_path, drive_d_path);
        return 1;
    }

    g_disk_controller = host_disk_controller();
    g_disk_controller_ready = true;

    memset(memory, 0x00, 64 * 1024);
    loadDiskLoader(0xff00);
    time_reset();
    i8080_reset(&cpu, terminal_read, terminal_write, sense_switches,
                &g_disk_controller, io_port_in, io_port_out);
    i8080_examine(&cpu, 0xff00);
    bus_switches = cpu.address_bus;

    /* In web mode, don't run the CPU until a browser is attached so the boot
       banner is delivered to the first client (mirrors the ESP32 firmware). */
    if (g_web_mode)
    {
        while (keep_running && !web_terminal_has_clients())
        {
            wait_ms(50);
        }
        if (keep_running)
        {
            fprintf(stderr, "[altair-local] Browser connected.\n");
        }
    }

    cpu_state_set_mode(CPU_RUNNING);

    while (keep_running)
    {
        if (cpu_state_get_mode() == CPU_RUNNING)
        {
            for (int i = 0; i < 4000 && keep_running; ++i)
            {
                i8080_cycle(&cpu);
            }
        }
        else
        {
            /* Stopped: terminal_read() returns 0 for the toggle byte,
               so any non-zero byte here is a monitor character. */
            uint8_t ch = terminal_read();
            if (ch != 0x00)
            {
                process_control_panel_commands_char(ch);
            }
        }
    }

    host_disk_close();
    if (g_web_mode)
    {
        web_terminal_stop();
    }
    else
    {
        host_terminal_restore();
    }
    return 0;
}
