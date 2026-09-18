<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CZet documentation

> Índice / Index. La documentación de usuario vive aquí; el overview técnico
> del binario autocontenido está en el `README.md` de la raíz.

## Guías / Guides

| Tema / Topic | Español | English |
|---|---|---|
| Macros `_Macro` (sintaxis nueva estilo `macro_rules!`) | [`GUIA_MACROS.md`](GUIA_MACROS.md) | [`CZET_MACRO_GUIDE.md`](CZET_MACRO_GUIDE.md) |
| Reflexión `_Reflect` → `reflect_t` (primitivo) | [`GUIA_REFLECT.md`](GUIA_REFLECT.md) | [`CZET_REFLECT_GUIDE.md`](CZET_REFLECT_GUIDE.md) |
| Stdlib propia `<utils/...>` (sin flags) | [`GUIA_STDLIB.md`](GUIA_STDLIB.md) | [`CZET_STDLIB_GUIDE.md`](CZET_STDLIB_GUIDE.md) |

## El lenguaje en 2 minutos / The language in 2 minutes

CZet es C ISO compatible más extensiones del compilador (activas con
`-std=czet`, que es el valor por defecto del driver `czet`):

- **`constexpr` con funciones** — `constexpr size_t factorial(int n) { ... }`
  se evalúa en compilación si los argumentos son constantes.
  Demo: [`tests/constexpr.czet`](../tests/constexpr.czet).
- **Genéricos monomorfizados** — `T sum<T>(T a, T b)`,
  `struct box<T,U>`, `union cell<T>`; se instancian como `sum<int>(...)`.
  Demo: [`tests/generics.czet`](../tests/generics.czet),
  [`tests/generic_function.czet`](../tests/generic_function.czet),
  [`tests/generic_memory.czet`](../tests/generic_memory.czet).
- **Sobrecarga de operadores** — `struct Vec2 _Operator(struct Vec2, "+") { ... }`
  con operandos `a`/`b` como punteros; el compilador reescribe cada uso.
  Demo: [`tests/operators.czet`](../tests/operators.czet).
- **`defer`** — `defer { ... };` ejecuta el bloque al salir del bloque
  (no solo de la función: también con `return`/`goto`/`break`/`continue`);
  `defer;` lo ejecuta ya. Demo: [`tests/defer.czet`](../tests/defer.czet).
- **Macros `_Macro`** — nueva sintaxis a nivel de tokens, con higiene,
  brazos y repetición `$(...)*|+|?`. Demos:
  [`tests/macro_rules.czet`](../tests/macro_rules.czet),
  [`tests/macro_repeat.czet`](../tests/macro_repeat.czet).
- **Reflexión `_Reflect`** — `_Reflect(T)` devuelve un `reflect_t` primitivo
  con `kind/size/align/name/count/elem/fields`.
  Demo: [`tests/reflect.czet`](../tests/reflect.czet).

## Compilar / Compiling

```text
czet -static tests/reflect.czet -o reflect && ./reflect
czet -static tests/macro_rules.czet -o macro_rules && ./macro_rules
```

El código nuevo debería usar `.ct`; `.czet` y `.c` plano también se aceptan
(`czet --help` para opciones y extensiones).

Con `-std=gnu23` las extensiones CZet (`_Reflect`, `_Operator`, `_Macro`,
`defer`, genéricos `f<T>`) quedan desactivadas y `_Reflect` pasa a ser un
identificador normal.

## Estructura del repo / Repo layout

- `README.md` (raíz) — filosofía + overview técnico del binario autocontenido.
- `src/czet.c` — driver estático autoextraíble.
- `src/gcc/gcc/c/` — frontend C modificado (`c-parser.cc`, `c-decl.cc`,
  `c-typeck.cc`, `c-fold.cc`, `c-tree.h`, `c-family/*`).
- `tools/embed.py`, `tools/czet-gcc` — empaquetado y wrapper legacy.
- `tests/*.ct` (`.czet` still accepted) — demos y tests de cada feature.
- `TODO.md` — pendientes (p. ej. `tests/variant.czet`).
