# The JOCKY Compiler (`jockyc`)

A native-code compiler for JOCKY, a statically typed systems language aimed at
digital-forensics / incident-response work. Source is parsed, type-checked, and
lowered to LLVM IR, then to a native executable.

```
.jky source ─▶ lexer ─▶ parser ─▶ AST ─▶ [type-check + LLVM codegen] ─▶ .o ─▶ link(runtime+libgc) ─▶ executable
```

## Layout

| Path | What |
| :--- | :--- |
| `spec/` | The language specification (source of truth). Start at `spec/00_design_decisions.md`. |
| `src/lexer.*`, `token.*` | Tokenizer. |
| `src/parser.*`, `ast.*` | Recursive-descent parser → AST. |
| `src/types.*` | The semantic type system. |
| `src/codegen.h`, `src/codegen/` | Type checker + LLVM IR generator, split by concern. See `ARCHITECTURE.md`. |
| `runtime/jocky_rt.{h,c}` | The C runtime (strings, bytes, list, map, logging, fs, panics). Linked into every program. |

See **`ARCHITECTURE.md`** for a file-by-file map of the whole compiler.
| `examples/` | Sample `.jky` programs. |
| `third_party/llvm/` | Vendored LLVM 23.1.3 (git-ignored, ~12 GB). |

## Build

Prerequisites: `cmake`, `ninja`, a C++20 compiler, and `libgc` (Boehm GC,
`bdw-gc`). LLVM is the vendored copy under `third_party/llvm`.

```sh
./build.sh          # configure + build into build/
# or manually:
cmake -S . -B build -G Ninja
cmake --build build
```

This produces `build/jockyc` (the compiler) and `build/libjocky_rt.a` (the
runtime, whose path is baked into the compiler so linking just works).

## Use

```sh
build/jockyc examples/triage.jky -o triage   # compile to a native binary
./triage

build/jockyc file.jky --emit-tokens          # token stream
build/jockyc file.jky --emit-ast             # parsed AST
build/jockyc file.jky --emit-llvm -o file    # LLVM IR -> file.ll
```

## Status (v0.1)

Implemented and working end-to-end:

- All primitive types (`int`, `float`, `byte`, `bool`, `void`) and reference
  types (`string`, `bytes`, `list[T]`, `map[K,V]`, user `struct`, `Error`).
- `fn` with multiple return values; `struct` declarations and literals.
- `if` / `else if` / `else`, `while`, `for … in` (over `list`/`bytes`).
- Full operator set with precedence; short-circuit `&&`/`||`.
- Type inference (`auto`), typed collections, string interpolation
  (`"{expr}"`), untyped numeric literals (design decisions D1–D4).
- Explicit casts per the spec's cast table; no implicit conversions.
- `nil` rules, nil-dereference and bounds-check runtime panics.
- `log` and `fs` standard-library modules; `JKY_ERR(msg)`.
- Compile-time diagnostics (`E0005`–`E0008`, parser `E02xx`, lexer `E01xx`);
  see `spec/error_codes.md`.

Deliberately provisional (C-like defaults, marked `[PROVISIONAL]` in
`spec/00_design_decisions.md`) until more syntax samples arrive: the exact
spelling of `main`, `else`/`while`, struct-literal syntax, `const`, and
`import`. Memory is Boehm GC for now (D5).

Not yet implemented: `--debug` overflow checks, the wider standard library,
annotations, user-defined generics, and string indexing.
