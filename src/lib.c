/**
 * Library entry point: includes_run() — same logic as main() without CLI parsing.
 */
#include "config.h"
#include "file_loader.h"
#include "include_collect.h"
#include "macro.h"
#include "toolkit.h"
#include "util/logging.h"
#include <stdlib.h>
#include <stdio.h>

int includes_run(const includes_config_t *config, void (*cb)(void *ctx, const char *path), void *ctx) {
	macro_table_t *macros;
	include_collect_t collect;
	size_t i;

	if (!config || config->sources_count == 0)
		return 2;

    set_loglevel(1 + config->verbosity_delta);

	file_loader_init(config);
	macros = macro_table_create();
	if (!macros) {
		file_loader_finish();
		return 3;
	}

	{
		char **sys_paths = NULL;
		size_t sys_count = 0, sys_cap = 0;
		const char *tk = config->toolkit && config->toolkit[0] ? config->toolkit : "gcc";
		if (toolkit_capture(tk, macros, &sys_paths, &sys_count, &sys_cap)) {
			file_loader_add_system_paths((const char **)sys_paths, sys_count);
		}
		for (i = 0; i < sys_count; i++) free(sys_paths[i]);
		free(sys_paths);
		{
			char *triplet = config->triplet ? (char *)config->triplet : toolkit_get_triplet(tk);
			if (triplet) {
				toolkit_triplet_to_macros(triplet, macros);
				if (!config->triplet) free(triplet);
			}
		}
	}

	include_collect_init(&collect, config, macros);
	if (cb)
		include_collect_set_output_cb(&collect, (void (*)(void *, const char *))cb, ctx);

	for (i = 0; i < config->sources_count; i++) {
		if (!include_collect_file(&collect, config->sources[i])) {
			logwarn_fmt("Cannot read or parse file: %s", config->sources[i]);
		}
		if (collect.parse_error) break;
	}

	include_collect_finish(&collect);
	macro_table_destroy(macros);
	file_loader_finish();

	return collect.parse_error ? 1 : 0;
}
