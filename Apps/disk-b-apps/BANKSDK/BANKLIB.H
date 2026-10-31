#ifndef BANKLIB_H
#define BANKLIB_H

#define BL_OK 0
#define BL_ABI 1
#define BL_IO 2
#define BL_FORMAT 3
#define BL_SIGNATURE 4
#define BL_MEMORY 5
#define BL_DISPATCH 6
#define BL_ARGUMENT 7
#define BL_BOUNDS 8
#define BL_CLEANUP 9
#define BL_LIFECYCLE 10
#define BL_EXHAUSTED 11
#define BL_CHECKSUM 12
#define BL_STALE 13
#define BL_FRAME_MAX 256
#define BL_SIGNATURE_MAX 255

/* Bank-1 adapters may use this per-call, non-repeating object token. */
#define BL_TOKEN_ADDRESS 0xE4D6

/* Preserve the six-character linker ABI and existing source clients. */
#define bank_library_open blopen
#define bank_library_close blclos
#define bank_library_export_count blcnt
#define bank_library_export_name blname
#define bank_library_export_signature blsig
#define bank_library_call blcall
#define bank_library_call_bound blbnd
#define bank_library_check blchk
#define bank_library_last_error blerr
#define bank_library_set_error blfail
#define BANK_LIBRARY_CALL_TOKEN_ADDRESS BL_TOKEN_ADDRESS

/* Zero-initialize one binding per immutable name/signature pair. */
typedef struct {
    unsigned int generation;
    unsigned int index;
} bank_library_binding_t;

unsigned int bank_library_open(const char *filename);
unsigned int bank_library_close(void);
unsigned int bank_library_export_count(void);
unsigned int bank_library_export_name(unsigned int index, char *name);
unsigned int bank_library_export_signature(unsigned int index, char *signature,
                                           unsigned int capacity);
unsigned int bank_library_call(const char *name, const char *signature,
                               void *frame, unsigned int bytes);
unsigned int bank_library_call_bound(bank_library_binding_t *binding,
                                     const char *name, const char *signature,
                                     void *frame, unsigned int bytes);
/* True only while the open library and pool epoch are still current. */
unsigned int bank_library_check(void);
unsigned int bank_library_last_error(void);
void bank_library_set_error(unsigned int error);

#endif
