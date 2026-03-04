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
#define PATH_SEG_MAX 256

/** Normalize path: resolve . and .., collapse slashes. No symlink resolution. Preserves absolute (leading /). Caller frees result. */
static char *path_normalize(const char *path) {
	size_t seg_len[PATH_SEG_MAX];
	const char *seg_ptr[PATH_SEG_MAX];
	int n = 0;
	const char *p = path;
	int absolute = (path && path[0] == '/');

	if (!path) return NULL;
	if (absolute) p++;
	while (*p) {
		while (*p == '/') p++;
		if (!*p) break;
		const char *start = p;
		while (*p && *p != '/') p++;
		size_t len = (size_t)(p - start);
		if (len == 1 && start[0] == '.')
			continue;
		if (len == 2 && start[0] == '.' && start[1] == '.') {
			if (n > 0)
				n--;
			else if (n < PATH_SEG_MAX) {
				seg_ptr[n] = start;
				seg_len[n] = 2;
				n++;
			}
			continue;
		}
		if (n < PATH_SEG_MAX) {
			seg_ptr[n] = start;
			seg_len[n] = len;
			n++;
		}
	}
	size_t total = absolute ? 1 : 0;
	for (int i = 0; i < n; i++)
		total += seg_len[i] + (i > 0 ? 1 : 0);
	char *out = (char *)malloc(total + 1);
	if (!out) return NULL;
	char *q = out;
	if (absolute) *q++ = '/';
	for (int i = 0; i < n; i++) {
		if (i) *q++ = '/';
		memcpy(q, seg_ptr[i], seg_len[i]);
		q += seg_len[i];
	}
	*q = '\0';
	return out;
}

static const includes_config_t *loader_config;
static char **system_include_paths;
static size_t system_include_paths_count;

/* Simple cache: array of (path_key, content). Key is path as given (no symlink resolution). Grows on demand. */
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

static const file_content_t *cache_get(const char *path_key) {
	size_t i;
	for (i = 0; i < file_cache_count; i++) {
		if (strcmp(file_cache[i].path, path_key) == 0)
			return &file_cache[i].content;
	}
	return NULL;
}

static bool cache_put(const char *path_key, char *data, size_t len) {
	if (file_cache_count >= file_cache_cap) {
		size_t new_cap = file_cache_cap ? file_cache_cap * 2 : CACHE_INIT;
		struct cache_entry *n = (struct cache_entry *)realloc(file_cache, new_cap * sizeof(*file_cache));
		if (!n) return false;
		file_cache = n;
		file_cache_cap = new_cap;
	}
	file_cache[file_cache_count].path = strdup(path_key);
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

	/* Quoted: 1) directory of including file, 2) -I, 3) system. Return path normalized (no symlink resolution). */
	if (!is_angled && including_dir) {
		if (try_path(including_dir, include_name, try, sizeof(try))) {
			resolved = path_normalize(try);
			free(including_dir);
			return resolved ? resolved : strdup(try);
		}
	}

	/* -I paths */
	if (loader_config) {
		for (i = 0; i < loader_config->include_paths_count; i++) {
			const char *d = loader_config->include_paths[i];
			if (try_path(d, include_name, try, sizeof(try))) {
				resolved = path_normalize(try);
				free(including_dir);
				return resolved ? resolved : strdup(try);
			}
		}
	}

	/* System paths */
	for (i = 0; i < system_include_paths_count; i++) {
		if (!system_include_paths[i]) continue;
		if (try_path(system_include_paths[i], include_name, try, sizeof(try))) {
			resolved = path_normalize(try);
			free(including_dir);
			return resolved ? resolved : strdup(try);
		}
	}

	free(including_dir);
	return NULL;
}

const file_content_t *file_read(const char *path) {
	char *path_key;
	FILE *f;
	size_t cap, n;
	char *data;
	long pos;
	const file_content_t *r = NULL;

	pthread_mutex_lock(&file_cache_mutex);
	path_key = path_normalize(path);
	if (!path_key) path_key = strdup(path);
	if (!path_key) goto done;

	if (cache_get(path_key)) {
		r = cache_get(path_key);
		free(path_key);
		goto done;
	}

	f = fopen(path_key, "rb");
	if (!f) {
		logdebug_fmt("Cannot open file: %s", path);
		free(path_key);
		goto done;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		free(path_key);
		goto done;
	}
	pos = ftell(f);
	if (pos < 0) {
		fclose(f);
		free(path_key);
		goto done;
	}
	rewind(f);
	cap = (size_t)pos;
	if (cap == 0) {
		data = (char *)malloc(1);
		if (data) data[0] = '\0';
		fclose(f);
		if (!data) { free(path_key); goto done; }
		cache_put(path_key, data, 0);
		r = cache_get(path_key);
		free(path_key);
		goto done;
	}
	data = (char *)malloc(cap + 1);
	if (!data) {
		fclose(f);
		free(path_key);
		goto done;
	}
	n = fread(data, 1, cap, f);
	fclose(f);
	if (n != cap) {
		free(data);
		free(path_key);
		goto done;
	}
	data[cap] = '\0';
	if (!cache_put(path_key, data, cap)) {
		free(data);
		free(path_key);
		goto done;
	}
	r = cache_get(path_key);
	free(path_key);
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

static char *try_dir_for_source(const char *dir, const char *base) {
	char try[PATH_MAX];
	static const char *exts[] = { ".c", ".cc", ".cpp", ".cxx" };
	size_t i;

	if (!dir || !base) return NULL;
	for (i = 0; i < sizeof(exts)/sizeof(exts[0]); i++) {
		int n = snprintf(try, sizeof(try), "%s/%s%s", dir, base, exts[i]);
		if (n > 0 && (size_t)n < sizeof(try) && access(try, R_OK) == 0) {
			char *out = path_normalize(try);
			return out ? out : strdup(try);
		}
	}
	return NULL;
}

char *file_header_to_source(const char *header_path, const char *including_path) {
	char *dir = NULL, *inc_dir = NULL, *base = NULL, *out = NULL;

	if (!header_path || !is_header_ext(header_path)) return NULL;
	base = basename_no_ext(header_path);
	if (!base) return NULL;
	dir = dir_of(header_path);
	if (!dir) { free(base); return NULL; }
	out = try_dir_for_source(dir, base);
	if (out) { free(dir); free(base); return out; }
	/* Do not try including file's directory when it differs from header's, or we can
	 * pick a same-named source from another module (e.g. demo/io/URI.cpp when header
	 * is demo/net/URI.hpp). Only try when same (redundant) so we never use wrong dir. */
	if (including_path && including_path[0]) {
		inc_dir = dir_of(including_path);
		if (inc_dir && strcmp(dir, inc_dir) == 0)
			out = try_dir_for_source(inc_dir, base);
		free(inc_dir);
	}
	free(dir);
	free(base);
	return out;
}
