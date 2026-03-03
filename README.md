includes
========

`includes` is a small CLI tool and C library that parses C/C++ source files, evaluates preprocessor conditionals, resolves `#include` directives, and prints the set of headers or mapped source files they depend on.

Features
--------

- Evaluates `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`
- Expands macros in `#if` expressions (including `defined(X)`)
- Distinguishes quoted vs angled includes and follows typical compiler search rules
- Supports recursive include graph traversal with depth limiting
- Optional header→source mapping mode (`-c`)
- JSON and Graphviz output modes
- Library API (`libincludes.a`, `include/includes.h`) exposing `includes_run()`

CLI usage
---------

Basic invocation:

```sh
includes [OPTIONS] SOURCES...
```

Common options:

- `-I PATH` add an include search path
- `-T TOOLKIT` choose compiler toolkit (`gcc`, `msc`)
- `-L NUM` maximum recursion depth (0 = only root)
- `-u` / `-a` show only quoted / only angled includes
- `-c` map header includes to corresponding source files (suppress root sources by default; use `-e` to also echo them)
- `-n` only show successfully resolved includes (ignored with `-c`)
- `-m` only show missing includes (ignored with `-c`)
- `-j` JSON output
- `-g` Graphviz `.dot` output

Examples
--------

List headers included (directly and recursively) from a source file:

```sh
includes src/main.c
```

Only show quoted (`"foo.h"`) includes, resolved recursively:

```sh
includes -u src/main.c
```

Map headers to corresponding source files in the same tree:

```sh
includes -c src/main.c
```

Show only missing includes (unresolved names):

```sh
includes -m src/main.c
```

Produce JSON output suitable for tooling:

```sh
includes -j src/main.c
```

Library usage
-------------

Link against `libincludes.a` and include the public header:

```c
#include <includes.h>
```

The library exposes `includes_run(const includes_config_t *config, void (*cb)(void *ctx, const char *path), void *ctx)`, which runs the same traversal logic as the CLI and invokes your callback for each resulting include path.

