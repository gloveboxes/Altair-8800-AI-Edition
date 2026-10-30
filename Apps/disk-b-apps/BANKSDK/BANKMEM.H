#ifndef BANKMEM_H
#define BANKMEM_H
/* Handles are offsets in bank 1, never C pointers. Zero means failure/NULL. */
typedef unsigned int bank_memory_handle_t;
typedef bank_memory_handle_t bhandle;

/* Preserve the six-character linker ABI and existing source clients. */
#define bank_memory_initialize binit
#define bank_memory_reset breset
#define bank_memory_allocate bmalloc
#define bank_memory_free bfree
#define bank_memory_size bsize
#define bank_memory_available bavail
#define bank_memory_execute bmexec
#define bank_memory_call bmcall
#define bank_memory_last_error bmeror
#define bank_memory_read bread
#define bank_memory_write bwrite

int bank_memory_initialize(void);
int bank_memory_reset(void); /* Destructive: invalidate all shared-pool handles. */
bank_memory_handle_t bank_memory_allocate(unsigned int bytes);
int bank_memory_free(bank_memory_handle_t handle);
unsigned int bank_memory_size(bank_memory_handle_t handle);
unsigned int bank_memory_available(void);
unsigned int bank_memory_last_error(void);
/* A zero stack handle selects the BIOS private stack. */
unsigned int bank_memory_execute(bank_memory_handle_t code_handle,
                                 bank_memory_handle_t stack_handle);
unsigned int bank_memory_call(bank_memory_handle_t code_handle,
                              bank_memory_handle_t stack_handle,
                              unsigned int entry_offset,
                              bank_memory_handle_t context_handle);
int bank_memory_read(bank_memory_handle_t handle, unsigned int offset,
                     void *buffer, unsigned int bytes);
int bank_memory_write(bank_memory_handle_t handle, unsigned int offset,
                      const void *buffer, unsigned int bytes);
#endif
