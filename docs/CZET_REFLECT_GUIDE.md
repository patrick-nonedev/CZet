<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CZet `_Reflect` Guide

`_Reflect` is a CZet compiler builtin that returns a `reflect_t` value
with the metadata of the reflected type. `reflect_t` (and its helper
`reflect_field_t`) are **primitive types**: they are always available,
no header or `typedef` needed, and work with `reflect_t` or
`struct reflect_t` spellings.

```c
reflect_t r = _Reflect(int);            /* a type */
reflect_t s = _Reflect(some_variable);  /* or an expression */
```

The full demo lives in `tests/reflect.czet`. Compile it with:

```text
czet -static tests/reflect.czet -o reflect
./reflect
   → [OK] reflect
```

---

## 1. Primitive types

```c
struct reflect_field_t {
    const char *name;       /* field name ("" for anonymous) */
    unsigned long offset;   /* byte offset within the struct/union,
                               or parameter index for functions */
    struct reflect_t *type; /* shallow reflect of the field/param type */
};

struct reflect_t {
    int kind;                          /* see §2 */
    unsigned long size;                /* sizeof, in bytes (0 for void/func) */
    unsigned long align;               /* _Alignof, in bytes (1 for void/func) */
    const char *name;                  /* type/tag/typedef name, or "" */
    unsigned long count;               /* see §4 */
    struct reflect_t *elem;            /* see §5 (NULL when absent) */
    struct reflect_field_t *fields;    /* see §6 (NULL when absent) */
};
```

Do **not** redeclare these types. An old first version of `_Reflect` required
a user `typedef struct { int kind; unsigned long size, align; } reflect_t;`;
that 3-field layout is still accepted for backward compatibility, but new
code should just use the primitive.

## 2. Kinds

| `kind` | meaning | example |
|---|---|---|
| `0` | other / unknown | — |
| `1` | void | `_Reflect(void)` |
| `2` | integer (`int`, `_Bool`, `_BitInt`, enums' underlying…) | `_Reflect(int)` |
| `3` | float (`float`, `double`, `_Complex`…) | `_Reflect(double)` |
| `4` | pointer | `_Reflect(int *)` |
| `5` | array | `_Reflect(int[10])` |
| `6` | struct | `_Reflect(struct Point)` |
| `7` | union | `_Reflect(union U)` |
| `8` | enum | `_Reflect(enum E)` |
| `9` | function | `_Reflect(int (int, int))` |
| `10` | typedef (direct use only, see §7) | `_Reflect(myint)` |

## 3. Syntax rules

1. Spelling is `_Reflect ( operand )` with mandatory parentheses, like
   `sizeof (T)`. Both `_Reflect(int)` and `_Reflect(x)` work.
2. The operand is **unevaluated** (like `sizeof`): `_Reflect(x++)` never
   increments `x`. Only its type is used.
3. Usable anywhere an expression fits: file scope initializers
   (`reflect_t g = _Reflect(double);`), function bodies, and nested
   expressions.
4. Active only under `-std=czet` (the `czet` driver default). With
   `-std=gnu23`, `_Reflect` is an ordinary identifier.
5. A bare `_Reflect` **without** `(` still parses as a variable name, so
   `int _Reflect = 42;` keeps compiling.
6. Reflecting an **incomplete type** is an error
   (`cannot reflect incomplete type`).

## 4. `count`

| kind | `count` |
|---|---|
| array | number of elements (`10` for `int[10]`; `0` for VLA/unknown) |
| struct / union | number of `FIELD_DECL` members |
| enum | number of enumerators |
| function | number of parameters (`0` for `void`) |
| otherwise | `0` |

## 5. `elem`

A pointer to a nested `reflect_t`, or `NULL`:

| kind | `elem` |
|---|---|
| array | element type (`int[10]` → `int`) |
| pointer | pointee type (`int *` → `int`) |
| typedef (`kind == 10`) | underlying type (`myint` → `int`) |
| function | return type (`int (int,int)` → `int`) |
| otherwise | `NULL` |

## 6. `fields`

A pointer to an array of `count` `reflect_field_t`, or `NULL`:

| kind | `fields` |
|---|---|
| struct / union | one entry per member: `{name, byte offset, type}` |
| function | one entry per parameter: `{name or "", index, type}` |
| otherwise | `NULL` |

Anonymous members get `name == ""`. Bitfields report the **byte** offset
only. Union members all report `offset == 0`.

## 7. Names and typedefs

- `name` is the tag/builtin name when there is one (`"int"`, `"Point"`,
  `"double"`), else `""` (pointers, arrays, plain function types).
- A `typedef` is only reported as `kind == 10` for a **direct**
  single-identifier use: `_Reflect(myint)` gives
  `{kind=10, name="myint", elem={kind=2, ...}}`.
  An expression of typedef type (`_Reflect(var_of_myint)`) decays to the
  underlying type (`kind=2`), because the typedef spelling is already lost
  — same as `sizeof` behavior.

## 8. Nesting (why it always terminates)

Nested reflects (`elem`, `fields[i].type`) are **shallow with a one-level
`elem` chain**: they carry `kind/size/align/name/count` and, for
pointer/array/typedef/function types, one further `elem`, but their own
`fields` is always `NULL`. So:

```c
struct Node { int v; struct Node *next; };
reflect_t n = _Reflect(struct Node);
/* n.fields[1] = {"next", off, kind=4}            */
/* n.fields[1].type->elem = {kind=6, name="Node"} */
/* (...->fields == NULL: no infinite recursion)   */

reflect_t p = _Reflect(int **);
/* p.kind=4 → p.elem->kind=4 → p.elem->elem->kind=2 */
```

You always get the direct data plus one useful level of pointee/element
data, never an infinite expansion.

## 9. Complete examples (all verified)

### 9.1 Struct introspection

```c
#include <stdio.h>

struct Point { int x, y; };

int main(void)
{
    reflect_t rp = _Reflect(struct Point);
    printf("struct %s size=%lu count=%lu\n", rp.name, rp.size, rp.count);
    for (unsigned long i = 0; i < rp.count; i++)
        printf("  %s off=%lu kind=%d size=%lu\n",
               rp.fields[i].name, rp.fields[i].offset,
               rp.fields[i].type->kind, rp.fields[i].type->size);
}
```

### 9.2 Pointer / array / typedef / function

```c
typedef int myint;

reflect_t pi = _Reflect(int *);      /* kind=4, elem->kind=2 */
reflect_t ar = _Reflect(int[10]);    /* kind=5, count=10, elem->kind=2 */
reflect_t td = _Reflect(myint);      /* kind=10, name="myint", elem->kind=2 */
reflect_t fn = _Reflect(int (int, int));
/* kind=9, count=2, elem->kind=2 (int), fields[0].type->kind=2 */
```

### 9.3 Expression form

```c
int x = 42;
reflect_t rx = _Reflect(x);          /* same as _Reflect(int) */
reflect_t re = _Reflect(x + 1);      /* type of the expression, unevaluated */
```

## 10. Compiling

```text
czet -static tests/reflect.czet -o reflect
./reflect
   → [OK] reflect
```

`tests/reflect.czet` is the section-by-section reference (struct loop,
pointer/array `elem`, union fields, typedef `kind=10`, function
return/params).
