# JOCKY

**JOCKY** is a statically typed, compiled systems language. Source is parsed,
type-checked, and lowered to LLVM IR, then to a native executable — no runtime
type checks, no interpreter.

> **Status: `v0.0.1-alpha`.** This is the first official milestone and the
> language is in active, early development. The alpha goal is a compiler that
> handles **basic-to-complex mathematics with ease** — expressions, numeric
> types, functions, and control flow — on a solid, well-structured foundation
> we build the rest of the language on. Expect sharp edges and breaking changes.

## Quick start

Requires `cmake`, `ninja`, a C++20 compiler, `libgc` (Boehm GC), and LLVM 23+
(`llvm-config` on your `PATH`). Optionally, `scripts/fetch-llvm.sh` vendors a
pinned LLVM under `third_party/` instead.

```sh
./build.sh                               # -> build/jockyc
build/jockyc examples/math.jky -o math   # compile a program to a native binary
./math                                   # run it
```

## A taste

```jocky
import log;

fn newton_sqrt(float x) -> float {
    float guess = x / 2.0;
    int i = 0;
    while i < 20 {
        guess = (guess + x / guess) / 2.0;
        i += 1;
    }
    return guess;
}

fn main() -> int {
    float r = newton_sqrt(2.0);
    log.info("sqrt(2) ~= {r}");
    return 0;
}
```

## The language, briefly

- **Value types:** `int` (i64), `float` (f64), `byte` (u8), `bool`, `void`.
- **Reference types:** `string`, `bytes`, `list[T]`, `map[K,V]`, `struct`, `Error`.
- **No implicit conversions** — every conversion between variables is an explicit
  cast `(type)expr`. Untyped numeric literals adapt to their context.
- **Type inference** (`auto`) inside function bodies; explicit types on signatures.
- **String interpolation:** `"value is {expr}"`.
- Braces and mandatory semicolons; `nil` only on reference types, with
  nil-dereference and bounds checks that panic rather than corrupt state.

See `spec/` for the full specification and `ARCHITECTURE.md` for a map of the
compiler internals.

## Layout

```
src/            compiler (lexer, parser, AST, types, codegen)
runtime/        C runtime linked into every compiled program
examples/       sample .jky programs
spec/           language specification
scripts/        tooling (e.g. fetch-llvm.sh)
```

## Docs

- `README.md` — this file
- `ARCHITECTURE.md` — file-by-file map of the compiler
- `COMPILER.md` — build details and feature status
- `ROADMAP.md` — where v0.0.1-alpha is headed
- `spec/` — the language specification
