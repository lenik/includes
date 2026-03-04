includes
========

`includes` is a small CLI tool and C library that parses C/C++ source files, evaluates preprocessor conditionals, resolves `#include` directives, and prints the set of headers or mapped source files they depend on.

Features
--------

- Evaluates `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`
- Expands macros in `#if` expressions (including `defined(X)`)
- Distinguishes quoted vs angled includes and follows typical compiler search rules
- Supports recursive include graph traversal with depth limiting
- Optional header→source mapping mode (`-c`) that recurses into source files (for compile automation)
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
- `-C DIR` change to directory before processing (like `make -C`)
- `-u` / `-a` show only quoted / only angled includes
- `-c` map headers to source files and **recurse into those sources** (transitive list for compile automation); root sources are omitted unless `-e` is used
- `-e` with `-c`, also output the source files listed on the command line
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

Map headers to source files and recurse into them (transitive list of all sources to build):

```sh
includes -c src/main.c
```

Use `-e` to include the root source(s) in the output. This mode is intended for compile-command automation (e.g. generating the list of `.c`/`.cpp` files for a target).

Show only missing includes (unresolved names):

```sh
includes -m src/main.c
```

Produce JSON output suitable for tooling:

```sh
includes -j src/main.c
```

Multi-file demo: how includes spread
------------------------------------

Suppose you have a small app:

```text
src/
  main.c        // #include "app.h"
  app.c         // #include "app.h", "util.h"
  app.h         // #include <stdio.h>
  util.c        // #include "util.h"
  util.h        // #include <stdlib.h>
```

To see how `#include` directives across these sources expand into a single set of dependencies:

```sh
includes src/main.c src/app.c src/util.c
```

Possible output:

```text
app.h
util.h
stdio.h
stdlib.h
```

If you instead want to know which **source** files must be built, use `-c` (and `-e` to include the roots). In `-c` mode, `includes` maps each header to its corresponding source (e.g. `foo.h` → `foo.c`) and **recurses into those source files** to find their includes, so you get a full transitive closure of sources:

```sh
includes -c -e src/main.c
```

which could yield:

```text
src/main.c
src/app.c
src/util.c
```

The list you can use in Makefile rules or compile scripts:

```Makefile
%.bin: %.c
	cc -o $@ `includes -c $^`
```

Change directory before processing (e.g. from a top-level Makefile):

```sh
includes -C build -c -e src/main.c
```

Library usage
-------------

Link against `libincludes.a` and include the public header:

```c
#include <includes.h>
```

The library exposes `includes_run(const includes_config_t *config, void (*cb)(void *ctx, const char *path), void *ctx)`, which runs the same traversal logic as the CLI and invokes your callback for each resulting include path.

