# JOCKY Language Spec — 1. Lexical Structure

## 1. Whitespace

In JOCKY, whitespace consists of:

- Spaces (`ASCII 0x20`)
- Horizontal tabs (`ASCII 0x09`)
- Carriage returns (`ASCII 0x0D`)
- Newlines (`ASCII 0x0A`)

Whitespace serves solely to separate otherwise adjacent tokens. Except within
string literals, multiple consecutive whitespace characters are treated as a
single whitespace separator and are ignored by the parser.

Unlike Python, **JOCKY is not indentation-sensitive**. Code blocks are
explicitly delimited by curly braces (`{` and `}`).

## 2. Token Categories

The lexer classifies tokens into four fundamental categories:

1. **Keywords** — `fn`, `struct`, `import`, `if`, `while`, `for`, `return`, `const`, `auto`
2. **Identifiers** — e.g. `case_id`, `process_list`, `SealEvidence`
3. **Literals** — e.g. `42`, `3.14159`, `"CASE-001"`, `x"9090AF"` (hex byte string)
4. **Operators & Punctuation** — e.g. `+`, `-`, `*`, `/`, `->`, `!`, `;`, `{`, `}`, `(`, `)`

## 3. Semicolons

In JOCKY, **semicolons (`;`) are strictly mandatory** at the termination of
every statement, variable declaration, import statement, and struct field
definition.

### Why Mandatory Semicolons?

Modern languages such as JavaScript, Python, and Go implement Automatic
Semicolon Insertion (ASI) or newline-driven parsing. While this saves
keystrokes, it introduces subtle, high-risk parsing ambiguities.

## 4. Comments & Documentation

JOCKY supports standard C-style single-line and multi-line comments.

### Single-Line Comments (`//`)

Single-line comments begin with two forward slashes `//` and extend to the end
of the current line:

```jocky
// This is a single-line comment explaining the triage logic
int timeout_ms = 5000; // Inline comment: 5-second network timeout
```

### Multi-Line Block Comments (`/* ... */`)

Block comments begin with `/*` and terminate with `*/`. They may span multiple
lines and can be placed anywhere whitespace is permitted:

```jocky
/*
 * Case File Metadata Aggregator
 * Target Agency: NTRO Cyber Operations
 * Classification: RESTRICTED
 */
fn process_bundle(string case_id) -> void {
    /* inline block comment */ log.info(case_id);
}
```

---

## Settled by Later Chapters

From [02_types.md](02_types.md):

- **More keywords and literals:** `true`, `false`, `nil`, `in` (as in
  `for x in xs`).
- **Integer literals:** decimal, hex `0x…` and binary `0b…`, with `_` digit
  separators (`0x7FFF_FFFF_0000`, `0b0000_1111`).
- **Char literals:** `'M'` exists.
- **Built-in type names:** `int`, `float`, `byte`, `bool`, `void`, `string`,
  `bytes`, `list`, `map`, `Error`.
- **Punctuation used in samples:** `.` (member access), `,` (multiple
  values), `[` `]` (indexing, list literals), `:` (map literals), and `=`,
  `==`, `!=`, `<`, `>`, `||`.

## Open Questions

Points the samples above don't settle yet:

- **Type names:** are `int`, `string`, etc. reserved keywords or built-in
  identifiers? If they're keywords, the parser can tell `(int)x` (a cast)
  apart from `(x)` (a parenthesised expression) without extra work.
- **Is the keyword list complete?** Still unconfirmed: `else`, `break`,
  `continue`, `enum`, etc.
- **Block comments**: can they nest (`/* /* */ */`)? C says no.
- **Doc comments**: does the section title "Documentation" imply a `///` or `/** */` form?
- **Literals**: octal `0o`? Number suffixes? Float exponents (`1e9`)?
  Escape sequences in strings and char literals? Does `x"..."` need an even
  number of hex digits?
- **Full operator set**: `==`, `!=`, `<=`, `&&`, `||`, `&`, `|`, `^`, `<<`, `>>`,
  `%`, `+=` etc., `.` (member access, as in `log.info`), `::`, `[]`, `,`, `=`.
- **Identifiers**: ASCII only (`[A-Za-z_][A-Za-z0-9_]*`) or Unicode?
