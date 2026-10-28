#ifndef BANKMEM_H
#define BANKMEM_H
/* Handles are offsets in bank 1, never C pointers. Zero means failure/NULL. */
typedef unsigned int bhandle;
int binit(void);
int breset(void); /* Destructive: invalidate all handles in this shared heap. */
bhandle bmalloc(unsigned int bytes);
int bfree(bhandle handle);
unsigned int bsize(bhandle handle);
unsigned int bavail(void);
unsigned int bmexec(bhandle code_handle, bhandle stack_handle); /* Zero uses private stack. */
unsigned int bmcall(bhandle code_handle, bhandle stack_handle,
                    unsigned int entry_offset, bhandle context_handle);
int bread(bhandle handle, unsigned int offset, void *buffer, unsigned int bytes);
int bwrite(bhandle handle, unsigned int offset, const void *buffer, unsigned int bytes);
#endif
