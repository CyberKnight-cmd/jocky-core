# JOCKY Design Decisions

Audience: digital-forensics / incident-response analysts. Goal: Python-level
ease of writing, with static types and native (LLVM) compilation — no runtime
type checks.

These decisions override conflicting text in earlier chapters; update the
chapters to match as we go.

## D1. Type inference inside functions; explicit types on signatures

`auto` means **type inference only** (it is no longer the dynamic "any" type).

```jocky
auto pid  = 1482;           // inferred int
auto name = "svchost.exe";  // inferred string
int  x    = 5;              // explicit form still allowed
```

Function signatures always spell out parameter and return types:

```jocky
fn is_suspicious(string path, int pid) -> bool { ... }
```

Rationale: low ceremony inside bodies (feels like Python), but signatures stay
explicit for fast compilation and good error messages.

## D2. Typed collections

`list` and `map` are **homogeneously typed generics** — the only generics in
v0.1; users cannot declare their own generic types.

```jocky
auto pids = [1024, 2048];              // inferred list[int]
int first = pids[0];                   // no cast — element type is known
for pid in pids { ... }                // pid : int

map[string, string] meta = { "host": "DC-PRIMARY" };
string h = meta["host"];               // no cast
```

- Element type is inferred from the literal, or written as `list[T]` /
  `map[K, V]`.
- An empty literal needs an annotation: `list[int] xs = [];`.
- Replaces the old "tagged union / cast on every read" model. A `list[int]` is
  a flat `i64` buffer in LLVM; no per-element tag, no runtime type check.
- Map keys in v0.1: `string`, `int`, `byte` (hashable value/immutable types).

This retires the cast-table row `auto → ConcreteType` and the need for
`(int)pids[0]`.

## D3. String interpolation

`"...{expr}..."` interpolates expressions. The compiler type-checks each
`expr`, converts it to `string` using the same rules as `(string)expr`, and
concatenates. Only types with a defined `→ string` cast may be interpolated;
others are a compile error.

```jocky
log.info("Processing PID {pid} on {host}");
// == "Processing PID " + (string)pid + " on " + host
```

- `{{` and `}}` are literal braces.
- Interpolation is compile-time expansion; no runtime format parsing.

## D4. Number literals are untyped constants

An integer or float literal has no fixed type until used. It adopts the type
required by its context and is a **compile error** if the value doesn't fit.

```jocky
byte opcode = 0x90;      // ok: fits in byte
byte bad    = 300;       // COMPILE ERROR E0008: literal 300 out of range for 'byte'
int  n      = 42;        // ok
float f     = 1;         // ok: integer literal usable where float is expected
```

- Applies to **literals only**. Variables still require explicit casts
  (`(byte)some_int`), preserving "no implicit conversions between variables".
- Default type when context gives none: `int` for integer literals, `float`
  for float literals. (e.g. `auto n = 42;` → `int`.)

This resolves the earlier conflict where `byte x = 0x90;` appeared to break the
no-implicit-conversion rule.

## D5. Automatic memory management

Reference types (`string`, `bytes`, `list`, `map`, `struct`, `Error`) are
heap-allocated and freed automatically. **v0.1 uses the Boehm–Demers–Weiser
conservative GC** (`libgc`) for speed of implementation. Analysts never call
`free`.

Revisit later: reference counting or arenas for long-running tools that need
predictable memory. The language surface does not change if we switch.

## D6. What stays from the safety model

Kept deliberately — small to learn, high safety payoff:

- Braces `{ }` for blocks; mandatory semicolons.
- No truthiness (`if n` is E0005).
- No implicit conversions between variables.
- `nil` only on reference types.
- Single 64-bit `int`.

---

## Provisional (C-like defaults until samples arrive) — marked [PROVISIONAL]

These are needed to compile whole programs; they will change when you send
samples. Each is tagged `[PROVISIONAL]` where it appears in the spec/code.

- **Entry point:** `fn main() -> int` (return 0 = success). A `-> void` main is
  also accepted and treated as returning 0.
- **`else` / `else if`:** C-style.
  ```jocky
  if c { ... } else if d { ... } else { ... }
  ```
- **`while`:** `while cond { ... }`.
- **Operators & precedence** (high → low): unary `!` `-` `(cast)`; `* / %`;
  `+ -`; `<< >>`; `< <= > >=`; `== !=`; `&`; `^`; `|`; `&&`; `||`; assignment
  `=` `+=` `-=` ... (lowest). Logical `&&`/`||` short-circuit.
- **Struct literals:** `CaseFile{ id: "C1", pid: 42 }`; field access `c.id`.
- **`const`:** `const int MAX = 65535;` — compile-time constant, no reassignment.
- **`import`:** `import fs;` brings a stdlib module into scope as `fs`.
