#ifndef INCLUDES_TOOLKIT_H
#define INCLUDES_TOOLKIT_H

#include "macro.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * Run compiler to get predefined macros and optionally system include paths.
 * toolkit_name: "gcc" or "msc" (NULL = try gcc).
 * Returns true on success. Injects macros into macro_table; paths (if non-NULL)
 * get system include dirs appended (caller allocates/owns the array).
 */
bool toolkit_capture(const char *toolkit_name, macro_table_t *macros,
                     char ***paths, size_t *path_count, size_t *path_cap);

/**
 * Get target triplet (e.g. x86_64-linux-gnu). Caller frees.
 */
char *toolkit_get_triplet(const char *toolkit_name);

/**
 * Add platform macros derived from triplet (e.g. __linux__, __x86_64__).
 * Triplet format: arch[-vendor]-os (e.g. x86_64-linux-gnu).
 */
void toolkit_triplet_to_macros(const char *triplet, macro_table_t *macros);

#endif /* INCLUDES_TOOLKIT_H */
