/* Raspberry Pi Sense HAT environmental sensors (HTS221 + LPS25H over I2C).
 *
 * Ported from the Altair-8800-Emulator reference project
 * (src/Drivers/sensehat-driver/src/sense_hat.h). Linux-only: requires access
 * to the /dev/i2c-<n> bus the Sense HAT is wired to (typically i2c-1).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Reads air pressure from LPS25H
int get_pressure(void);

// Reads temperature from LPS25H
float get_temperature_from_lps25h(void);

// Reads the temperature from HTS221
float get_temperature(void);

// Reads the humidity from HTS221
float get_humidity(void);

// Initializes i2c on the given bus number (e.g. 1 for /dev/i2c-1).
// Returns non-zero on success.
int pi_sense_hat_sensors_init(int i2c_num);

// Closes the i2c file descriptors.
void pi_sense_hat_sensors_close(void);

#ifdef __cplusplus
}
#endif
