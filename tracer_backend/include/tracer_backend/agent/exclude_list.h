// Exclude list API - fast hash set for symbol names
#ifndef ADA_EXCLUDE_LIST_H
#define ADA_EXCLUDE_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque exclude list
typedef struct AdaExcludeList AdaExcludeList;

// Create/destroy
AdaExcludeList* ada_exclude_create(size_t capacity_hint);
void ada_exclude_destroy(AdaExcludeList* xs);

// Hash helper (FNV-1a 64-bit, case-insensitive)
uint64_t ada_exclude_hash(const char* name);

// Mutators
bool ada_exclude_add(AdaExcludeList* xs, const char* name);
void ada_exclude_add_defaults(AdaExcludeList* xs);
void ada_exclude_add_from_csv(AdaExcludeList* xs, const char* csv);

// Queries
bool ada_exclude_contains(AdaExcludeList* xs, const char* name);
bool ada_exclude_contains_hash(AdaExcludeList* xs, uint64_t hash);

#ifdef __cplusplus
}
#endif

#endif // ADA_EXCLUDE_LIST_H

