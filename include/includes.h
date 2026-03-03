/**
 * Public API for libincludes — resolve #include directives from C/C++ sources.
 *
 * Build: link with libincludes.a (or the same object files as the CLI).
 * Include path: add include/ and src/ so that config.h and this header are found.
 *
 * Example:
 *   includes_config_t config;
 *   config_init(&config);
 *   config.include_paths = ...; config.sources = ...; config.sources_count = 1;
 *   int r = includes_run(&config, my_callback, my_ctx);
 *   config_free(&config);
 */
#ifndef INCLUDES_H
#define INCLUDES_H

#include <stddef.h>

/* Use the same config struct as the CLI; include path must provide config.h */
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Callback invoked for each included file path (resolved). */
typedef void (*includes_cb)(void *ctx, const char *resolved_path);

/**
 * Run include resolution on the given config (sources and options must be set).
 * For each included file, calls cb(ctx, path). If cb is NULL, paths are printed to stdout.
 * Returns: 0 success, 1 parse error, 2 file/usage error, 3 internal error.
 */
int includes_run(const includes_config_t *config, includes_cb cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDES_H */
