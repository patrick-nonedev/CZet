<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CoBaMacros `_Macro` Guide

`_Macro` macros define **new syntax** for your program, in the spirit of
Rust's `macro_rules!`: you declare a call pattern and an expansion, and the
compiler rewrites the call at the *token* level before parsing.

```text
_Macro NAME< $p: fragment, ... > {
  ( call form A ) { expansion A }
  ( call form B ) { expansion B }      <- multiple "arms"
};
```

You invoke it with `NAME<arguments...>` anywhere C fits: at file scope (it
generates declarations), inside functions (it generates statements) and in
expressions. If **no** arm matches, the tokens are handed back to the parser
as-is and you get an ordinary syntax error.

Definitions are **file-scope only**, local to the translation unit, and cannot
be redefined. The full demo lives in `tests/macro_rules.czet`.

---

## 1. Parameters (fragments)

```c
$name: fragment
```

| fragment | what it captures |
|---|---|
| `ident`   | a single name (`foo`, `_bar`, `N3`…) |
| `expr`    | a balanced expression; stops at a top-level comma |
| `type`    | same as `expr`, meant for types |
| `stmt`    | a balanced run of tokens; *may* swallow commas |
| `block`   | a complete balanced `{ ... }` block |
| `tokens`  | a raw balanced run of tokens; *may* swallow commas |
| `number`  | exactly one numeric literal (`42`, `0x1A`, `3.5`) |
| `string`  | exactly one string literal (`"hola"`) |
| `literal` | any single literal (number, char, string…) |

Example:

```c
_Macro STRUCT<$name: ident, $b: block> {
  (STRUCT $name , $b) { struct $name $b; }
};

STRUCT<circle, { double x, y, radius; int color; }>
   → struct circle { double x, y, radius; int color; };
```

`expr`/`type` shrink their capture with *backtracking*: if the rest of the
pattern refuses to match, they retry with a shorter capture (longest-first).

## 2. Repetition

```text
$( $p: fragment ) SEP? OP        OP = *  |  +  |  ?
```

- `*` = zero or more iterations, `+` = one or more, `?` = zero or one.
- `SEP` is the separator **between iterations at the call site** (`,`, `;`,
  anything; it can be omitted, e.g. whitespace-separated ident lists).
- Groups nest: `$( $( $x ),+ );+` replicates both levels.

**Golden rule:** a repeated group captures **one metavariable per iteration**.
To capture "tuples", use several **parallel** repetitions; the expansion
group aligns them by index (see PORTS in §7).

**Strict separators:** the separators you write at the call site must appear
verbatim in the pattern. `(ALL_IS $fname , $b)` goes with
`ALL_IS<all_circle, {...}>`; if the pattern is `(LIST $t , $($x),*)`, the call
must be `LIST<arr, 1, 2>`.

**Lengths:** two metavariables inside the same group must repeat the same
number of times (`meta-variable repeats N times but another repeats M`).

## 3. Arms

Arms are tried **in order**; the first match wins. The **macro name is part
of the pattern** (it always starts with `NAME`):

```c
_Macro DIAG<$a: expr, $b: expr> {
  (DIAG $a)          { printf("  DIAG<1>: %d\n", $a); }
  (DIAG $a , $b)     { printf("  DIAG<2>: %d %d\n", $a, $b); }
};

DIAG<1>     // first arm
DIAG<2, 3>  // second arm
```

You can dispatch on **literal tokens** in the pattern: `(LOG info $msg)`,
`(KIND int)`, etc. This is how you juggle "several types" without generics
(see §5).

## 4. Hygiene

Names the macro **body declares** are renamed to `name__czet<N>` per expansion,
with `N` unique per invocation:

```c
_Macro STEP<$n: number> {
  (STEP $n) { static int steps; steps += $n; printf("  steps=%d\n", steps); }
};
STEP<10>  // static int steps__czet0
STEP<20>  // static int steps__czet1  (its own)
```

**Never** renamed:
- whatever the caller captured (the `$param`s of the call);
- names used *by reference*: `printf`, `struct`/`union`/`enum` tags,
  `typedef` aliases;
- the name and parameters of a function the body generates (the macro's
  "contract").

**Important gotcha:** *fields* and *variables* the body declares are renamed
too. If outside code must see them, pass their name as a `$param: ident` from
the call (provenance ⇒ no rename), or declare the type outside the macro
(see PORTS in §7).

## 5. Can CZet generics be used in macros?

**Yes — they compose.** A macro defines *syntax*; CZet's generics
(`T f<T>(...)`, `struct box<T,U>`) define *type abstraction*. The macro itself
is not generic (there is no `_Macro FOO<T>`), but:

- the `$t: type` fragment captures a type, and the body can **declare and
  instantiate** generics with it:

```c
T sum<T>(T a, T b) { return a + b; }
struct box<T, U> { T a; U b; };

_Macro BOX<$t: type, $i: ident> {
  (BOX $t , $i) { struct box<$t, $t> $i; }
};
BOX<int, counters>
   → struct box<int, int> counters;

_Macro SUM3<$t: type, $a: expr, $b: expr, $c: expr> {
  (SUM3 $t , $a , $b , $c) { sum<$t>(($a), ($b)) + ($c) }
};
SUM3<int, 10, 20, 30>   → sum<int>((10), (20)) + (30)
```

**And often you don't need generics at all.** C types everything itself:
capture the type with `$t: type` and paste it; the same body serves `int`,
`double` or a `struct` (SWAP, §7). For "dispatching over several types" you
also have **arms** with literal tokens (KIND, LOG) and, for operator
overloading, CZet's `_Operator`. Generics shine when you need *synhesis*:
a fresh monomorphized function/struct per type.

## 6. Syntax rules

1. The definition ends with `;` after the final `}`.
2. Inside `<...>`, nested `<`/`>` and `<<`/`>>` are handled; `()`, `[]`, `{}`
   protect their contents.
3. The call must match the pattern **token by token** (order and commas).
4. Recursive expansion is capped at 200 levels
   (`macro expansion nested too deeply`).
5. **Limitation:** an expansion whose first token is `(` does not work in
   expression position (the injected `(` confuses the parser). Start with a
   keyword (`int`, `if`, `sizeof`…) or, for a compound literal, wrap it:
   `sizeof((int[]){ ... })` (see `VEC_LEN` in the demo).

## 7. Complete examples (all verified)

### 7.1 `LIST` — a `vec!`-style array generator

```c
_Macro LIST<$t: ident, $( $x: expr ),*> {
  (LIST $t , $($x),*) { int $t[] = { $($x),* }; }
};

int main(void) {
  LIST<cnt, 3, 1, 4, 1, 5>
  printf("%d\n", cnt[2]);   // 4
}
```

This shape (ident + group) requires the pattern to spell the comma after
`$t` and the call to carry it: `LIST<cnt, 3, 1, 4, 1, 5>`.

### 7.2 `LOG` — variants by "prefix word" (level)

```c
_Macro LOG<$lvl: ident, $m: string> {
  (LOG error $m) { fprintf(stderr, "[E] %s\n", $m); }
  (LOG warn  $m) { fprintf(stderr, "[W] %s\n", $m); }
  (LOG info  $m) { printf("[I] %s\n", $m); }
};

LOG<info "ready">
LOG<error "boom">
```

Note: each arm may use just a **subset** of the declared parameters.

### 7.3 `KIND` — arms by type name + fallback

```c
_Macro KIND<$t: ident> {
  (KIND int)    { printf("  integer\n"); }
  (KIND double) { printf("  float\n"); }
  (KIND $t)     { printf("  other\n"); }
};

KIND<int>
KIND<double>
KIND<foo>
```

### 7.4 `PORTS` — a mini-DSL (parallel repetitions + hygiene)

```c
struct row { const char *key; int value; };   /* type declared OUTSIDE */

_Macro PORTS<$t: ident, $( $name: string ),* , $( $port: number ),*> {
  (PORTS $t $( $name ),* ; $( $port ),*) {
    static const struct row $t[] = { $( { $name, $port } ),* };
  }
};

PORTS<rows "http", "https", "dns" ; 80, 443, 53>
// → static const struct row rows[] = { { "http", 80 },
//                                     { "https", 443 }, { "dns", 53 } };
```

- `struct row` is declared outside so hygiene doesn't rename the fields.
- `$t` (the table name) comes from the caller → never renamed.
- The two parallel repetitions are paired by index in the expansion.

### 7.5 `SWAP` — no generics, one body fits all

```c
_Macro SWAP<$t: type, $a: ident, $b: ident> {
  (SWAP $t , $a , $b) { $t __tmp; __tmp = $a; $a = $b; $b = __tmp; }
};

SWAP<int, x, y>
SWAP<double, d1, d2>
```

The same body types `int`, `double`, structs… C resolves it; no
monomorphization needed.

### 7.6 `BOX` / `SUM3` — composing with generics (see §5)

### 7.7 Mapping typical Rust macros to CZet

| Rust | CZet |
|---|---|
| `vec![a, b, c]` | `LIST<xs, a, b, c>` (array), `VEC_LEN<...>` (count) |
| `assert!(x)` / `assert_eq!(a, b)` | `ASSERT_TRUE<a>` / `ASSERT_EQ<a, b>` (demo) |
| `println!("{}", x)` | `PRINT_VALS<...>` / `LOG<lvl "msg">` |
| `todo!()` / `unreachable!()` | a `$t: tokens` macro that emits the call |
| static tables (e.g. `lazy_static`) | `PORTS<...>` / `TABLE<...>` |
| shape-`match` | arms: `(DIAG $a)` vs `(DIAG $a , $b)` |

## 8. Compiling

```text
czet -static tests/macro_rules.czet -o macro_rules
./macro_rules
   → Demo completa terminada.
```

`tests/macro_rules.czet` is the section-by-section reference (1–2 basics,
3–5 fragments, 6–10 repetition, 11 hygiene, 12 provenance, 13 arms).