#ifndef HOST_FILES_IO_H
#define HOST_FILES_IO_H

#include <stddef.h>
#include <stdint.h>

void host_files_init(const char *apps_root);
void host_files_close(void);
void host_files_out(uint8_t port, uint8_t data);
uint8_t host_files_in(uint8_t port);

/* Private selectors 60/61 used by shared-service group 0x70. */
size_t files_output(int port, uint8_t data, char *buffer, size_t buffer_length);
uint8_t files_input(uint8_t port);

#endif
