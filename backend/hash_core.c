#include "hash_core.h"
#include <ctype.h>

void normalize_key(const char* input, char* output, size_t max_len) {
    if (!input || !output || max_len == 0) return;

    size_t i = 0;
    while (input[i] != '\0' && i < max_len - 1) {
        output[i] = (char)toupper((unsigned char)input[i]);
        i++;
    }
    output[i] = '\0';
}

size_t compute_hash_bucket(const char* key, size_t bucket_count) {
    if (!key || bucket_count == 0) return 0;

    size_t h = 0;
    for (size_t i = 0; key[i] != '\0'; i++) {
        unsigned char c = (unsigned char)toupper((unsigned char)key[i]);
        h = (h * 31u + c) % bucket_count;
    }
    return h;
}
