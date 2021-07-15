#ifndef ORIGINAL
#include <limits.h>
int             compute_sha256(unsigned char *src, unsigned int src_len, unsigned char *buffer);
void            hash2hex(unsigned char hash[32], char hex[65]);
void            ExtractHomedir(char dest[PATH_MAX]);
#endif
void            handle_request(int, struct clinfo *);