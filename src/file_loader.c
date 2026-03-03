#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include "file_loader.h"
#include "config.h"
#include "util/logging.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#ifdef __linux__
#include <linux/limits.h>
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const includes_config_t *loader_config;
static char **system_include_paths;
static size_t system_include_paths_count;

/* Simple cache: array of (canonical_path, content). Grows on demand. */
#define CACHE_INIT 32
static struct cache_entry {
	char *path;
	file_content_t content;
} *file_cache;
static size_t file_cache_count;
static size_t file_cache_cap;
static pthread_mutex_t file_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static void cache_free_entry(struct cache_entry *e) {
	free(e->path);
	free(e->content.data);
	e->path = NULL;
	e->content.data = NULL;
	e->content.len = 0;
}

static const file_content_t *cache_get(const char *canonical) {
	size_t i;
	for (i = 0; i < file_cache_count; i++) {
		if (strcmp(file_cache[i].path, canonical) == 0)
			return &file_cache[i].content;
	}
	return NULL;
}

static bool cache_put(const char *canonical, char *data, size_t len) {
	if (file_cache_count >= file_cache_cap) {
		size_t new_cap = file_cache_cap ? file_cache_cap * 2 : CACHE_INIT;
		struct cache_entry *n = (struct cache_entry *)realloc(file_cache, new_cap * sizeof(*file_cache));
		if (!n) return false;
		file_cache = n;
		file_cache_cap = new_cap;
	}
	file_cache[file_cache_count].path = strdup(canonical);
	file_cache[file_cache_count].content.data = data;
	file_cache[file_cache_count].content.len = len;
	if (!file_cache[file_cache_count].path) {
		free(data);
		return false;
	}
	file_cache_count++;
	return true;
}

void file_loader_init(const struct includes_config *config) {
	loader_config = config;
	system_include_paths = NULL;
	system_include_paths_count = 0;
	file_cache = NULL;
	file_cache_count = file_cache_cap = 0;
}

void file_loader_add_system_paths(const char **paths, size_t count) {
	size_t i;
	if (count == 0) return;
	/* Replace for simplicity; could extend. */
	for (i = 0; i < system_include_paths_count; i++)
		free(system_include_paths[i]);
	free(system_include_paths);
	system_include_paths = (char **)malloc(count * sizeof(char *));
	if (!system_include_paths) return;
	system_include_paths_count = count;
	for (i = 0; i < count; i++)
		system_include_paths[i] = paths[i] ? strdup(paths[i]) : NULL;
}

void file_loader_finish(void) {
	size_t i;
	loader_config = NULL;
	for (i = 0; i < system_include_paths_count; i++)
		free(system_include_paths[i]);
	free(system_include_paths);
	system_include_paths = NULL;
	system_include_paths_count = 0;
	for (i = 0; i < file_cache_count; i++)
		cache_free_entry(&file_cache[i]);
	free(file_cache);
	file_cache = NULL;
	file_cache_count = file_cache_cap = 0;
}

char *file_canonical_path(const char *path) {
	char buf[PATH_MAX];
	if (realpath(path, buf) == NULL)
		return NULL;
	return strdup(buf);
}

static char *dir_of(const char *path) {
	const char *p = strrchr(path, '/');
	if (!p) return strdup(".");
	return strndup(path, (size_t)(p - path));
}

static bool try_path(const char *dir, const char *name, char *out, size_t out_size) {
	int n;
	if (!dir || !name) return false;
	n = snprintf(out, out_size, "%s/%s", dir, name);
	if (n < 0 || (size_t)n >= out_size) return false;
	return access(out, R_OK) == 0;
}

char *file_resolve_include(const char *including_path, const char *include_name, bool is_angled) {
	char *resolved = NULL;
	char *including_dir = NULL;
	char try[PATH_MAX];
	size_t i;

	if (!include_name || !*include_name) return NULL;

	including_dir = dir_of(including_path);

	/* Quoted: 1) directory of including file, 2) -I, 3) system */
	if (!is_angled && including_dir) {
		if (try_path(including_dir, include_name, try, sizeof(try))) {
			resolved = file_canonical_path(try);
			free(including_dir);
			return resolved;
		}
	}

	/* -I paths */
	if (loader_config) {
		for (i = 0; i < loader_config->include_paths_count; i++) {
			const char *d = loader_config->include_paths[i];
			if (try_path(d, include_name, try, sizeof(try))) {
				resolved = file_canonical_path(try);
				free(including_dir);
				return resolved;
			}
		}
	}

	/* System paths */
	for (i = 0; i < system_include_paths_count; i++) {
		if (!system_include_paths[i]) continue;
		if (try_path(system_include_paths[i], include_name, try, sizeof(try))) {
			resolved = file_canonical_path(try);
			free(including_dir);
			return resolved;
		}
	}

	free(including_dir);
	return NULL;
}

const file_content_t *file_read(const char *path) {
	char *canonical;
	FILE *f;
	size_t cap, n;
	char *data;
	long pos;
	const file_content_t *r = NULL;

	pthread_mutex_lock(&file_cache_mutex);
	canonical = file_canonical_path(path);
	if (!canonical) goto done;

	if (cache_get(canonical)) {
		r = cache_get(canonical);
		free(canonical);
		goto done;
	}

	f = fopen(path, "rb");
	if (!f) {
		logdebug_fmt("Cannot open file: %s", path);
		free(canonical);
		goto done;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		free(canonical);
		goto done;
	}
	pos = ftell(f);
	if (pos < 0) {
		fclose(f);
		free(canonical);
		goto done;
	}
	rewind(f);
	cap = (size_t)pos;
	if (cap == 0) {
		data = (char *)malloc(1);
		if (data) data[0] = '\0';
		fclose(f);
		if (!data) { free(canonical); goto done; }
		cache_put(canonical, data, 0);
		r = cache_get(canonical);
		free(canonical);
		goto done;
	}
	data = (char *)malloc(cap + 1);
	if (!data) {
		fclose(f);
		free(canonical);
		goto done;
	}
	n = fread(data, 1, cap, f);
	fclose(f);
	if (n != cap) {
		free(data);
		free(canonical);
		goto done;
	}
	data[cap] = '\0';
	if (!cache_put(canonical, data, cap)) {
		free(data);
		free(canonical);
		goto done;
	}
	r = cache_get(canonical);
	free(canonical);
done:
	pthread_mutex_unlock(&file_cache_mutex);
	return r;
}

static bool is_header_ext(const char *path) {
	const char *dot = strrchr(path, '.');
	/* Treat paths with no extension as headers too (e.g. C++ standard
	 * library headers like <vector>, <iostream>). */
	if (!dot) return true;
	return (strcmp(dot, ".h") == 0) || (strcmp(dot, ".hpp") == 0) || (strcmp(dot, ".hxx") == 0);
}

static char *basename_no_ext(const char *path) {
	const char *slash = strrchr(path, '/');
	const char *base = slash ? slash + 1 : path;
	const char *dot = strrchr(base, '.');
	if (!dot) return strdup(base);
	return strndup(base, (size_t)(dot - base));
}

char *file_header_to_source(const char *header_path) {
	char *dir = NULL, *base = NULL, *out = NULL;
	char try[PATH_MAX];
	static const char *exts[] = { ".c", ".cc", ".cpp", ".cxx" };
	size_t i;

	if (!header_path || !is_header_ext(header_path)) return NULL;
	dir = dir_of(header_path);
	base = basename_no_ext(header_path);
	if (!dir || !base) goto done;
	for (i = 0; i < sizeof(exts)/sizeof(exts[0]); i++) {
		int n = snprintf(try, sizeof(try), "%s/%s%s", dir, base, exts[i]);
		if (n > 0 && (size_t)n < sizeof(try) && access(try, R_OK) == 0) {
			out = file_canonical_path(try);
			if (!out) out = strdup(try);
			break;
		}
	}
done:
	free(dir);
	free(base);
	return out;
}
