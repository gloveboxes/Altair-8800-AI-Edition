#ifndef CHATJSON_H
#define CHATJSON_H

#include <stddef.h>
#include <stdint.h>

/* Chat JSON request and response support for dcc C11. */

/* Message types */
enum message_type {
	MSG_SYS = 0,
	MSG_USR = 1,
	MSG_AST = 2
};

/* Message length limits */
#define USR_LEN 256
#define AST_LEN 8192
#define MSG_LEN AST_LEN  /* History rows use assistant-length capacity */

int j_genr(const char *system_message, const uint8_t *types, char *const *texts,
		   size_t message_count, const char *model, const char *max_tokens,
		   const char *temperature, char *output, size_t output_size);

#endif
