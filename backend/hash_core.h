#ifndef HASH_CORE_H
#define HASH_CORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Converts a key to uppercase into `output` for case-insensitive hashing.
void normalize_key(const char* input, char* output, size_t max_len);

// Polynomial rolling hash (base 31) returning a bucket index in [0, bucket_count - 1].
size_t compute_hash_bucket(const char* key, size_t bucket_count);

#ifdef __cplusplus
}
#endif

#endif // HASH_CORE_H
