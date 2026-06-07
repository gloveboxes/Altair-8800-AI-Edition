/* Raspberry Pi Sense HAT 8x8 RGB LED panel driver (Linux framebuffer).
 *
 * Ported from the Altair-8800-Emulator reference project
 * (src/Drivers/pi_sense_hat/led_panel.c).
 */

#include "led_panel.h"

static int fbfd = -1;
static uint16_t *map;
static uint16_t *p;
static struct fb_fix_screeninfo fix_info;
static bool panel_initialized;

static bool open_8x8_panel(const char *filepath)
{
    /* open the led frame buffer device */
    fbfd = open(filepath, O_RDWR);
    if (fbfd == -1) {
        return false;
    }

    /* read fixed screen info for the open device */
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &fix_info) == -1) {
        close(fbfd);
        fbfd = -1;
        return false;
    }

    /* now check the correct device has been found */
    if (strcmp(fix_info.id, "RPi-Sense FB") != 0) {
        close(fbfd);
        fbfd = -1;
        return false;
    }

    return true;
}

static bool init_8x8_panel(void)
{
    /* The Sense HAT LED matrix is exposed by the rpisense-fb kernel driver as
       a framebuffer whose fix_info.id is "RPi-Sense FB". Its /dev/fbN number is
       not fixed: on a Pi with no display it is often fb0, but on a Pi 4/5 with
       KMS the main display takes fb0 and the Sense HAT lands on fb1 or higher.
       An explicit device can be forced with ALTAIR_SENSE_HAT_FB (path or N);
       otherwise scan /dev/fb0../dev/fb31 and match by id. */
    bool found  = false;
    const char *forced = getenv("ALTAIR_SENSE_HAT_FB");
    if (forced != NULL && forced[0] != '\0') {
        char path[32];
        if (forced[0] == '/') {
            snprintf(path, sizeof(path), "%s", forced);
        } else {
            snprintf(path, sizeof(path), "/dev/fb%s", forced);
        }
        found = open_8x8_panel(path);
        fprintf(stderr, "[sense-hat] LED matrix: forced device %s -> %s\n",
                path, found ? "matched 'RPi-Sense FB'" : "not a Sense HAT framebuffer");
    } else {
        for (int i = 0; i < 32 && !found; i++) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/fb%d", i);
            found = open_8x8_panel(path);
            if (found) {
                fprintf(stderr, "[sense-hat] LED matrix: found 'RPi-Sense FB' at %s\n", path);
            }
        }
        if (!found) {
            fprintf(stderr,
                    "[sense-hat] LED matrix: scanned /dev/fb0../dev/fb31, "
                    "none reported id 'RPi-Sense FB'.\n");
        }
    }

    if (!found) {
        return false;
    }

    /* map the led frame buffer device into memory */
    map = mmap(NULL, PI_SENSE_8x8_BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (map == MAP_FAILED) {
        close(fbfd);
        fbfd = -1;
        return false;
    }

    /* set a pointer to the start of the memory area */
    p = map;

    /* clear the led matrix */
    memset(map, 0, PI_SENSE_8x8_BUFFER_SIZE);

    return true;
}


bool pi_sense_hat_init(void)
{
    if (panel_initialized) {
        return true;
    }

    if (!init_8x8_panel()) {
        return false;
    }

    panel_initialized = true;
    return true;
}

bool pi_sense_8x8_panel_update(uint16_t *panel_buffer, size_t buffer_len)
{
    if (!panel_initialized) {
        return false;
    }

    if (buffer_len != PI_SENSE_8x8_BUFFER_SIZE) {
        return false;
    }

    memcpy(map, panel_buffer, PI_SENSE_8x8_BUFFER_SIZE);

    return true;
}

void pi_sense_hat_close(void)
{
    if (!panel_initialized) {
        return;
    }

    if (map && map != MAP_FAILED) {
        munmap(map, PI_SENSE_8x8_BUFFER_SIZE);
    }

    if (fbfd >= 0) {
        close(fbfd);
    }

    map               = NULL;
    p                 = NULL;
    fbfd              = -1;
    panel_initialized = false;
}
