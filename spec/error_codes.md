# JOCKY Error Code Registry

Compile-time diagnostics referenced by the spec. Add new codes here as they appear.

| Code  | Message / Meaning                                                    | Defined in       |
| :---- | :------------------------------------------------------------------- | :--------------- |
| E0005 | Non-`bool` used as a condition (e.g. `if count { ... }`)             | 02_types.md §2   |
| E0006 | Cannot assign `nil` to primitive value type (e.g. `int x = nil;`)    | 02_types.md §7   |
| E0007 | Binary operator cannot be applied to mismatched types (e.g. `int + float`) | 02_types.md §9 |
| E0008 | Numeric literal out of range for its target type (e.g. `byte x = 300;`) | 00_design_decisions.md D4 |

E0001–E0004 are unassigned so far.

## Lexer (E01xx)

| Code  | Message / Meaning                              |
| :---- | :--------------------------------------------- |
| E0100 | Unexpected character                           |
| E0101 | Numeric literal (hex/binary) has no digits     |
| E0102 | Unterminated string literal                    |
| E0103 | Unterminated block comment                     |
| E0104 | `\x` escape needs two hex digits               |
| E0105 | Unterminated / multi-char char literal         |
| E0106 | Unknown escape in char literal                 |
| E0107 | Invalid/unterminated hex byte literal          |
| E0108 | Hex byte literal has an odd number of digits   |
