# Roadmap

## v0.0.1-alpha — "math with ease" (current)

The goal of the first alpha is a compiler that handles basic-to-complex
mathematics cleanly, on a foundation solid enough to grow the rest of the
language from.

Already working (carried over from the prototype):

- `int` / `float` / `byte` arithmetic, comparisons, bitwise ops, with correct
  precedence and short-circuit `&&` / `||`.
- Explicit numeric casts (`(int)`, `(float)`, `(byte)`); no implicit conversions.
- Untyped numeric literals that adapt to context, with range checks.
- Functions (incl. multiple return values), `if` / `else`, `while`, `for … in`.
- `auto` inference, string interpolation for readable numeric output.

Targets to reach `v0.0.1`:

- [ ] A `math` standard-library module: `sqrt`, `pow`, `abs`, `floor`, `ceil`,
      `round`, `min`, `max`, and the constants `PI` / `E`.
- [ ] Integer division-by-zero and overflow handling defined and tested
      (panic in `--debug`, documented wrapping in release).
- [ ] Float formatting good enough for real output (precision, inf/NaN).
- [ ] A focused test suite of math programs with expected output.
- [ ] Nail down the still-`[PROVISIONAL]` surface the math subset touches
      (operators, `const`, `main`), promoting them from provisional to specified.

## Later (beyond the alpha, not yet scheduled)

- Broader standard library, modules/imports beyond the built-ins.
- Richer error handling ergonomics.
- The systems / forensics direction the prototype explored (bytes, `fs`, …)
  carried forward deliberately rather than by accident.

The prototype that preceded this repo lives separately; this repo is the
official line of development.
