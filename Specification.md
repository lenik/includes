# `includes` — Specification

## 1. Overview

`includes` is a CLI tool written in C that:

* Recursively parses C/C++ source files
* Resolves `#include` directives
* Evaluates conditional compilation using macros
* Outputs the list of included filenames
* Optionally maps header files to corresponding source files

It is designed to be:

* Deterministic
* Compiler-aware (GCC/MSVC compatible macro environment)
* Build-system friendly (Makefile integration)
* Suitable for packaging (Debian-ready)

---

## 2. High-Level Architecture

```
CLI
 ├── Option Parser
 ├── Toolkit / Triplet Detector
 ├── Macro Environment
 ├── Preprocessor Engine
 │     ├── Lexer
 │     ├── Directive Parser
 │     ├── Conditional Evaluator
 │     └── Include Resolver
 ├── File Graph Manager
 ├── Header→Source Translator (-c)
 ├── Deduplication Layer
 └── Output Formatter
```

---

## 3. Core Functional Requirements

### 3.1 Input

```
includes [OPTIONS] SOURCES...
```

* One or more source files
* C or C++ extensions:

  * `.c`
  * `.cc`
  * `.cpp`
  * `.cxx`
  * `.h`
  * `.hpp`
  * `.hxx`

---

## 4. Include Parsing Semantics

### 4.1 Supported Directives

* `#include "file"`
* `#include <file>`
* `#if`
* `#ifdef`
* `#ifndef`
* `#elif`
* `#else`
* `#endif`
* `#define`
* `#undef`

### 4.2 Conditional Handling

The tool must:

* Maintain macro symbol table
* Evaluate integer expressions in `#if`
* Support:

  * `defined(X)`
  * `&&`
  * `||`
  * `!`
  * `==`, `!=`, `<`, `>`, `<=`, `>=`
  * Arithmetic operators

The preprocessor must ensure:

* Only active branches produce includes
* Inactive branches are skipped entirely

---

## 5. Include Resolution Rules

### 5.1 Search Path

Search order:

For `"file"`:

1. Directory of including file
2. `-I` paths (in order)
3. System include paths

For `<file>`:

1. `-I` paths
2. System include paths

System include paths are discovered from:

* `-T toolkit`
* Default compiler in `PATH`

---

## 6. Toolkit Detection

### 6.1 `-T toolkit`

Supported:

* `gcc`
* `msc`

Behavior:

* Execute compiler with:

  * `gcc -dM -E - < /dev/null`
* Capture predefined macros
* Insert into macro environment

### 6.2 `-t triplet`

Examples:

* `x86_64-apple-macosx14.0.0`
* `x86_64-linux-gnu`

Used to:

* Add platform macros
* Adjust include search paths

Auto-detected if not specified.

Verbose mode must show:

* Discovered toolkit
* Discovered triplet

---

## 7. Recursive Traversal

### 7.1 Default Behavior

* `#include "file"` parsed recursively (`-U` default)
* `#include <file>` parsed recursively only with `-A`

### 7.2 Depth Control

`-L NUM`

* Stops recursion beyond NUM levels
* Depth 0 = only root file

---

## 8. Deduplication

By default:

* Unique include names only

With `-d`:

* Allow duplicates

Implementation:

Use hash set keyed by canonicalized path or raw name depending on output mode.

---

## 9. Output Modes

### 9.1 Name Filtering

* `-u` → quoted only
* `-a` → angled only
* default → both

### 9.2 Quote Preservation

`-p` preserves:

```
"file.h"
<file.h>
```

Default output:

```
file.h
```

### 9.3 Header→Source Translation (`-c`)

If:

```
#include "subdir/foo.h"
```

And exists:

```
subdir/foo.c
subdir/foo.cpp
subdir/foo.cxx
```

Output:

```
subdir/foo.cxx
```

Rules:

* Only search same directory as header
* Priority order configurable (default: `.c`, `.cc`, `.cpp`, `.cxx`)
* Ignore if no source exists

---

## 10. Data Structures

### 10.1 File Node

```c
typedef struct include_node {
    char *path;
    int depth;
    bool is_system;
    struct include_node **children;
} include_node;
```

### 10.2 Macro Table

```c
typedef struct macro {
    char *name;
    char *value;
} macro_t;
```

Use:

* Hash table
* String interning recommended

---

## 11. CLI Behavior Specification

### Exit Codes

| Code | Meaning        |
| ---- | -------------- |
| 0    | Success        |
| 1    | Parse error    |
| 2    | File error     |
| 3    | Internal error |

