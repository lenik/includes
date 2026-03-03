#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include "file_loader.h"
#include "include_collect.h"
#include "macro.h"
#include "toolkit.h"
#include "util/logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define PARALLEL_GROW 64

typedef struct {
	char **paths;
	size_t n;
	size_t cap;
	int parse_error;
} parallel_result_t;

static void parallel_append_cb(void *ctx, const char *path) {
	parallel_result_t *r = (parallel_result_t *)ctx;
	if (r->n >= r->cap) {
		size_t new_cap = r->cap ? r->cap * 2 : PARALLEL_GROW;
		char **p = (char **)realloc(r->paths, new_cap * sizeof(char *));
		if (!p) return;
		r->paths = p;
		r->cap = new_cap;
	}
	r->paths[r->n] = path ? strdup(path) : NULL;
	if (r->paths[r->n]) r->n++;
}

static void *parallel_worker(void *arg) {
	const includes_config_t *config = ((void **)arg)[0];
	const char *source_path = ((void **)arg)[1];
	macro_table_t *initial_macros = ((void **)arg)[2];
	macro_table_t *macros;
	include_collect_t collect;
	parallel_result_t *result;

	result = (parallel_result_t *)calloc(1, sizeof(*result));
	if (!result) return NULL;
	macros = macro_table_clone(initial_macros);
	if (!macros) { free(result); return NULL; }
	include_collect_init(&collect, config, macros);
	include_collect_set_output_cb(&collect, parallel_append_cb, result);
	if (!include_collect_file(&collect, source_path))
		logwarn_fmt("Cannot read or parse file: %s", source_path);
	result->parse_error = collect.parse_error;
	include_collect_finish(&collect);
	macro_table_destroy(macros);
	return result;
}

int main(int argc, char **argv) {
	includes_config_t config;
	int first_arg;
	int r;

	config_init(&config);
	r = config_parse_options(&config, argc, argv, &first_arg);

	if (r == 1) {
		config_print_usage();
		config_free(&config);
		return 0;
	}
	if (r != 0) {
		config_print_usage();
		config_free(&config);
		return 2; /* internal/usage error */
	}

	app_logger.level = 1 + config.verbosity_delta;
	if (app_logger.level < 0) app_logger.level = 0;
	if (app_logger.level > 5) app_logger.level = 5;

	if (config.sources_count == 0) {
		logwarn_fmt("No source files given");
		config_print_usage();
		config_free(&config);
		return 2;
	}

	logdebug_fmt("Sources: %zu, -I count: %zu, depth: %d",
		config.sources_count, config.include_paths_count, config.max_depth);

	file_loader_init(&config);
	macro_table_t *macros = macro_table_create();
	if (!macros) {
		file_loader_finish();
		config_free(&config);
		return 3;
	}
	{
		char **sys_paths = NULL;
		size_t sys_count = 0, sys_cap = 0;
		const char *tk = config.toolkit && config.toolkit[0] ? config.toolkit : "gcc";
		if (toolkit_capture(tk, macros, &sys_paths, &sys_count, &sys_cap)) {
			file_loader_add_system_paths((const char **)sys_paths, sys_count);
		}
		for (size_t i = 0; i < sys_count; i++) free(sys_paths[i]);
		free(sys_paths);
		{
			char *triplet = config.triplet ? (char *)config.triplet : toolkit_get_triplet(tk);
			if (triplet) {
				toolkit_triplet_to_macros(triplet, macros);
				if (app_logger.level >= 2) {
					loginfo_fmt("Toolkit: %s", tk);
					loginfo_fmt("Triplet: %s", triplet);
				}
				if (!config.triplet) free(triplet);
			}
		}
	}
	if (config.parallel && config.sources_count > 1) {
		pthread_t *threads = (pthread_t *)malloc(config.sources_count * sizeof(pthread_t));
		void **worker_args = (void **)malloc(config.sources_count * 3 * sizeof(void *));
		parallel_result_t **results = (parallel_result_t **)calloc(config.sources_count, sizeof(parallel_result_t *));
		int exit_code = 0;
		size_t i, j;

		if (!threads || !worker_args || !results) {
			free(threads);
			free(worker_args);
			free(results);
			macro_table_destroy(macros);
			file_loader_finish();
			config_free(&config);
			return 3;
		}
		for (i = 0; i < config.sources_count; i++) {
			worker_args[i * 3 + 0] = (void *)&config;
			worker_args[i * 3 + 1] = (void *)config.sources[i];
			worker_args[i * 3 + 2] = (void *)macros;
			pthread_create(&threads[i], NULL, parallel_worker, &worker_args[i * 3]);
		}
		for (i = 0; i < config.sources_count; i++) {
			pthread_join(threads[i], (void **)&results[i]);
			if (results[i] && results[i]->parse_error) exit_code = 1;
		}
		for (i = 0; i < config.sources_count; i++) {
			if (results[i]) {
				for (j = 0; j < results[i]->n; j++) {
					printf("%s\n", results[i]->paths[j]);
					free(results[i]->paths[j]);
				}
				free(results[i]->paths);
				free(results[i]);
			}
		}
		free(threads);
		free(worker_args);
		free(results);
		macro_table_destroy(macros);
		file_loader_finish();
		config_free(&config);
		return exit_code;
	}

	include_collect_t collect;
	include_collect_init(&collect, &config, macros);
	for (size_t i = 0; i < config.sources_count; i++) {
		if (!include_collect_file(&collect, config.sources[i])) {
			logwarn_fmt("Cannot read or parse file: %s", config.sources[i]);
		}
		if (collect.parse_error) break;
	}
	include_collect_finish(&collect);
	macro_table_destroy(macros);
	file_loader_finish();

	config_free(&config);
	return collect.parse_error ? 1 : 0;
}
