#ifndef INCLUDES_FILE_LOADER_H
#define INCLUDES_FILE_LOADER_H

#include <stddef.h>
#include <stdbool.h>

struct includes_config;

typedef struct file_content {
	char *data;
	size_t len;
} file_content_t;

/**
 * Initialize loader with config (copies include paths etc.). Optional.
 * If not called, resolve_include uses only the paths passed to it.
 */
void file_loader_init(const struct includes_config *config);
void file_loader_finish(void);

/**
 * Return canonical absolute path for path, or NULL on failure.
 * Caller must free the result.
 */
char *file_canonical_path(const char *path);

/**
 * Resolve an include name to a full path.
 * For "file": search order = including_dir, then -I paths, then system.
 * For <file>: -I paths, then system.
 * Returns allocated path or NULL if not found. Caller frees.
 */
char *file_resolve_include(const char *including_path, const char *include_name, bool is_angled);

/**
 * Read file at path into memory. Uses internal cache keyed by canonical path.
 * Returns content (owned by cache; do not free) or NULL. content->data is not NUL-terminated
 * if file has NUL bytes; use content->len. For text files, caller can treat as C string if
 * a trailing NUL is needed (e.g. copy or ensure buffer has extra byte).
 */
const file_content_t *file_read(const char *path);

/**
 * Optional: add system include paths (from toolkit). Called after file_loader_init.
 */
void file_loader_add_system_paths(const char **paths, size_t count);

/**
 * If path is a header, look for a corresponding source file (.c, .cc, .cpp, .cxx).
 * Search order: header's directory, then (if non-NULL) including_path's directory.
 * Returns allocated path or NULL. Caller frees.
 */
char *file_header_to_source(const char *header_path, const char *including_path);

#endif /* INCLUDES_FILE_LOADER_H */
