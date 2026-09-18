<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Guía de `_Reflect` en CZet

`_Reflect` es un builtin del compilador que devuelve un valor `reflect_t`
con la metadata del tipo reflejado. `reflect_t` (y su auxiliar
`reflect_field_t`) son **tipos primitivos**: siempre están disponibles,
sin headers ni `typedef`, y valen tanto `reflect_t` como `struct reflect_t`.

```c
reflect_t r = _Reflect(int);            /* un tipo */
reflect_t s = _Reflect(una_variable);   /* o una expresión */
```

La demo completa está en `tests/reflect.czet`. Se compila con:

```text
czet -static tests/reflect.czet -o reflect
./reflect
   → [OK] reflect
```

---

## 1. Tipos primitivos

```c
struct reflect_field_t {
    const char *name;       /* nombre del campo ("" si anónimo) */
    unsigned long offset;   /* offset en bytes en struct/union,
                               o índice del parámetro en funciones */
    struct reflect_t *type; /* reflect superficial del tipo del campo */
};

struct reflect_t {
    int kind;                          /* ver §2 */
    unsigned long size;                /* sizeof en bytes (0 en void/func) */
    unsigned long align;               /* _Alignof en bytes (1 en void/func) */
    const char *name;                  /* nombre de tipo/tag/typedef, o "" */
    unsigned long count;               /* ver §4 */
    struct reflect_t *elem;            /* ver §5 (NULL si no aplica) */
    struct reflect_field_t *fields;    /* ver §6 (NULL si no aplica) */
};
```

**No** redeclares estos tipos. La primerísima versión de `_Reflect` pedía
`typedef struct { int kind; unsigned long size, align; } reflect_t;`;
ese layout de 3 campos se sigue aceptando por compatibilidad, pero el código
nuevo debe usar el primitivo directamente.

## 2. Kinds

| `kind` | significado | ejemplo |
|---|---|---|
| `0` | otro / desconocido | — |
| `1` | void | `_Reflect(void)` |
| `2` | entero (`int`, `_Bool`, `_BitInt`…) | `_Reflect(int)` |
| `3` | flotante (`float`, `double`, `_Complex`…) | `_Reflect(double)` |
| `4` | puntero | `_Reflect(int *)` |
| `5` | array | `_Reflect(int[10])` |
| `6` | struct | `_Reflect(struct Point)` |
| `7` | union | `_Reflect(union U)` |
| `8` | enum | `_Reflect(enum E)` |
| `9` | función | `_Reflect(int (int, int))` |
| `10` | typedef (solo uso directo, ver §7) | `_Reflect(myint)` |

## 3. Reglas de sintaxis

1. Se escribe `_Reflect ( operando )` con paréntesis obligatorios, como
   `sizeof (T)`. Valen `_Reflect(int)` y `_Reflect(x)`.
2. El operando **no se evalúa** (como `sizeof`): `_Reflect(x++)` nunca
   incrementa `x`. Solo se usa su tipo.
3. Vale en cualquier expresión: inicializadores a nivel de fichero
   (`reflect_t g = _Reflect(double);`), cuerpos de función y expresiones
   anidadas.
4. Solo activo con `-std=czet` (el valor por defecto del driver `czet`).
   Con `-std=gnu23`, `_Reflect` es un identificador normal.
5. Un `_Reflect` **sin** `(` sigue siendo una variable, así que
   `int _Reflect = 42;` sigue compilando.
6. Reflejar un tipo **incompleto** es error
   (`cannot reflect incomplete type`).

## 4. `count`

| kind | `count` |
|---|---|
| array | nº de elementos (`10` en `int[10]`; `0` en VLA/desconocido) |
| struct / union | nº de miembros `FIELD_DECL` |
| enum | nº de enumeradores |
| función | nº de parámetros (`0` en `void`) |
| resto | `0` |

## 5. `elem`

Puntero a otro `reflect_t`, o `NULL`:

| kind | `elem` |
|---|---|
| array | tipo elemento (`int[10]` → `int`) |
| puntero | tipo apuntado (`int *` → `int`) |
| typedef (`kind == 10`) | tipo subyacente (`myint` → `int`) |
| función | tipo de retorno (`int (int,int)` → `int`) |
| resto | `NULL` |

## 6. `fields`

Puntero a un array de `count` `reflect_field_t`, o `NULL`:

| kind | `fields` |
|---|---|
| struct / union | una entrada por miembro: `{name, offset en bytes, type}` |
| función | una entrada por parámetro: `{name o "", índice, type}` |
| resto | `NULL` |

Los miembros anónimos llevan `name == ""`. Los bitfields reportan solo el
offset en **bytes**. En unions todos los `offset` son `0`.

## 7. Nombres y typedefs

- `name` es el nombre del tag/tipo cuando existe (`"int"`, `"Point"`,
  `"double"`); si no, `""` (punteros, arrays, tipos función pelados).
- Un `typedef` solo sale como `kind == 10` en uso **directo** con un único
  identificador: `_Reflect(myint)` da
  `{kind=10, name="myint", elem={kind=2, ...}}`.
  Una expresión de tipo typedef (`_Reflect(var_de_myint)`) decae al
  subyacente (`kind=2`), porque el spelling ya se perdió — igual que `sizeof`.

## 8. Anidado (por qué siempre termina)

Los reflects anidados (`elem`, `fields[i].type`) son **superficiales con un
nivel de `elem`**: traen `kind/size/align/name/count` y, si son
puntero/array/typedef/función, un `elem` más, pero su propio `fields`
siempre es `NULL`. Así:

```c
struct Node { int v; struct Node *next; };
reflect_t n = _Reflect(struct Node);
/* n.fields[1] = {"next", off, kind=4}            */
/* n.fields[1].type->elem = {kind=6, name="Node"} */
/* (...->fields == NULL: sin recursión infinita)  */

reflect_t p = _Reflect(int **);
/* p.kind=4 → p.elem->kind=4 → p.elem->elem->kind=2 */
```

Siempre tienes el dato directo más un nivel útil de pointee/elemento,
nunca una expansión infinita.

## 9. Ejemplos completos (todos verificados)

### 9.1 Introspección de un struct

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

### 9.2 Puntero / array / typedef / función

```c
typedef int myint;

reflect_t pi = _Reflect(int *);      /* kind=4, elem->kind=2 */
reflect_t ar = _Reflect(int[10]);    /* kind=5, count=10, elem->kind=2 */
reflect_t td = _Reflect(myint);      /* kind=10, name="myint", elem->kind=2 */
reflect_t fn = _Reflect(int (int, int));
/* kind=9, count=2, elem->kind=2 (int), fields[0].type->kind=2 */
```

### 9.3 Forma con expresión

```c
int x = 42;
reflect_t rx = _Reflect(x);          /* igual que _Reflect(int) */
reflect_t re = _Reflect(x + 1);      /* tipo de la expresión, sin evaluar */
```

## 10. Compilar

```text
czet -static tests/reflect.czet -o reflect
./reflect
   → [OK] reflect
```

`tests/reflect.czet` es la referencia sección por sección (bucle de struct,
`elem` de puntero/array, fields de union, `kind=10` de typedef,
retorno/parámetros de función).
