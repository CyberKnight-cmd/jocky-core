# JOCKY Compiler Architecture

A map of the codebase so you can find the right file fast when upgrading.

## The pipeline, one stage per box

```
source.jky
   │
   ▼  src/lexer.cpp            characters → tokens
 tokens
   │
   ▼  src/parser.cpp           tokens → AST          (grammar lives here)
  AST  (src/ast.h)
   │
   ▼  src/codegen/*            AST → LLVM IR          (type-checks while lowering)
LLVM IR
   │
   ▼  src/codegen/driver.cpp   IR → object file (.o)  (LLVM backend)
   │
   ▼  src/main.cpp             links .o + runtime     (shells out to the C compiler)
 executable
```

The runtime (`runtime/jocky_rt.c`) is the C code every compiled program links
against: strings, bytes, lists, maps, logging, `fs`, and panics.

## Where things live

Each file has one job. To change X, open the file that owns X:

| You want to change… | Open |
| :--- | :--- |
| A new token, keyword, operator, literal form | `src/lexer.cpp`, `src/token.h` |
| Grammar / what syntax is legal | `src/parser.cpp` (+ node shapes in `src/ast.h`) |
| The AST node kinds | `src/ast.h` / `src/ast.cpp` (+ `--emit-ast` printer) |
| What types exist and how they compare | `src/types.h` / `src/types.cpp` |
| A new error message / code | the file that detects it; register in `spec/error_codes.md` |
| Driver flags, the compile pipeline | `src/main.cpp` |

### Code generation, split by concern (`src/codegen/`)

`CodeGen` (public, `src/codegen.h`) is a thin facade over `CodeGen::Impl`, the
one object holding all codegen state. `Impl` is defined in
`src/codegen/internal.h` and its methods are implemented across these files:

| File | Owns |
| :--- | :--- |
| `internal.h` | The `Impl` definition: LLVM context/module/builder, symbol tables, scopes, and the small inline helpers (`err`, scopes, `rt`, `emitBlock`, `emitCond`). |
| `driver.cpp` | Construction, the `run()` pipeline, target-machine setup, and writing `.ll` / `.o`. |
| `lowering.cpp` | JOCKY-type → LLVM-type lowering, `resolveType`, the coercion rules (no implicit conversions, untyped literals), runtime-function signatures (`rtSig`), list/map slot packing. |
| `decls.cpp` | Declaration passes (structs, function signatures, globals) and function skeletons (entry block, parameter allocas). |
| `expr.cpp` | Every expression: literals, operators, casts, indexing, member access, list/map/struct literals, interpolation, and the three call forms (user functions, module builtins, methods). |
| `stmt.cpp` | Statements: variable declarations, assignment, `if`/`while`/`for`, `return`. |

A rule of thumb for `Impl` methods: trivial one-liners stay inline in
`internal.h`; anything with real logic is **declared** there and **defined** in
the matching `.cpp`, so there's exactly one place to look.

## Why the front end isn't split further

`lexer.cpp`, `parser.cpp`, `ast.cpp`, and `types.cpp` are each one cohesive
responsibility at a readable size (≈80–670 lines). A recursive-descent parser
in particular is a tightly-coupled set of mutually-recursive methods; splitting
it across files hurts more than it helps. Only the back end was large enough to
warrant breaking up, which is why just `src/codegen/` is a directory.

## Build & data flow at a glance

- `CMakeLists.txt` compiles the front-end `.cpp` + the five `src/codegen/*.cpp`
  into `jockyc`, and builds `runtime/jocky_rt.c` into `libjocky_rt.a`.
- The runtime archive path and the C compiler are baked into `jockyc` as
  compile definitions, so `jockyc file.jky -o out` can do the final link with
  no extra flags.
- `Diagnostic` (`src/diagnostic.h`) is the shared error type all phases push
  into; the driver prints them uniformly.
