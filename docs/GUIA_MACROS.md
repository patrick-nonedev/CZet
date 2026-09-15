<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Guía de macros `_Macro` en CZet

Las macros `_Macro` definen **sintaxis nueva** para tu programa, al estilo de
`macro_rules!` de Rust: declaras un patrón de llamada y una expansión, y el
compilador sustituye la llamada a nivel de *tokens* antes de parsear.

```text
_Macro NOMBRE< $p: fragmento, ... > {
  ( forma de llamada A ) { expansión A }
  ( forma de llamada B ) { expansión B }      <- varios "brazos"
};
```

La invocación es `NOMBRE<argumentos...>` y funciona en cualquier sitio: nivel
de fichero (genera declaraciones), dentro de funciones (genera sentencias) y
en expresiones. Si **ningún** brazo casa, los tokens se devuelven al parser
tal cual y te toca un error de sintaxis normal.

Las definiciones **solo** valen a nivel de fichero (`file scope`), son locales
al translation unit y no se pueden redefinir. La demo completa está en
`tests/macro_rules.slt`.

---

## 1. Parámetros (fragmentos)

```c
$nombre: fragmento
```

| fragmento | qué captura |
|---|---|
| `ident`   | un único nombre (`foo`, `_bar`, `N3`…) |
| `expr`    | una expresión balanceada; se detiene en la coma de nivel superior |
| `type`    | igual que `expr`, pensado para tipos |
| `stmt`    | un run de tokens balanceado; *puede* tragar comas |
| `block`   | un bloque `{ ... }` completo y balanceado |
| `tokens`  | un run crudo de tokens balanceado; *puede* tragar comas |
| `number`  | exactamente un literal numérico (`42`, `0x1A`, `3.5`) |
| `string`  | exactamente un literal de cadena (`"hola"`) |
| `literal` | cualquier literal simple (número, char, string…) |

Ejemplo:

```c
_Macro STRUCT<$name: ident, $b: block> {
  (STRUCT $name , $b) { struct $name $b; }
};

STRUCT<circle, { double x, y, radius; int color; }>
   → struct circle { double x, y, radius; int color; };
```

`expr` y `type` recortan con *backtracking*: si el resto del patrón no casa,
prueban con una captura más corta (longest-first).

## 2. Repetición

```text
$( $p: fragmento ) SEP? OP        OP = *  |  +  |  ?
```

- `*` = cero o más iteraciones, `+` = una o más, `?` = cero o una.
- `SEP` es el separador entre iteraciones **en la llamada** (`,`, `;`, lo que
  sea; puede omitirse, p. ej. listas de idents separados por espacios).
- Los grupos se anidan: `$( $( $x ),+ );+` replica dos niveles.

**Regla de oro:** un grupo repetido captura **un solo metavariable** por
iteración. Para capturar "tuplas", declara varias repeticiones **paralelas**;
el grupo de expansión las empareja por índice (ver ejemplo PORTS, sección 7).

**Comas estrictas:** los separadores que escribas en la llamada deben aparecer
literalmente en el patrón. `(ALL_IS $fname , $b)` con `ALL_IS<all_circle, {...}>`;
si el patrón es `(LIST $t , $($x),*)`, la llamada debe ser `LIST<arr, 1, 2>`.

**Longitudes:** dos metavariables dentro del mismo grupo deben repetir igual
número de veces (error: `meta-variable repeats N times but another repeats M`).

## 3. Brazos (arms)

Se prueban **en orden**; gana el primero que case. El **nombre de la macro
forma parte del patrón** (empieza siempre por `NOMBRE`):

```c
_Macro DIAG<$a: expr, $b: expr> {
  (DIAG $a)          { printf("  DIAG<1>: %d\n", $a); }
  (DIAG $a , $b)     { printf("  DIAG<2>: %d %d\n", $a, $b); }
};

DIAG<1>     // primer brazo
DIAG<2, 3>  // segundo brazo
```

Puedes despachar por **tokens literales** del patrón: `(LOG info $msg)`,
`(KIND int)`, etc. Así se gestionan "varios tipos" sin genéricos (ver §6).

## 4. Higiene

Los nombres que el cuerpo de la macro **declara** se renombran a
`nombre__czet<N>` en cada expansión, con `N` único por invocación:

```c
_Macro STEP<$n: number> {
  (STEP $n) { static int steps; steps += $n; printf("  steps=%d\n", steps); }
};
STEP<10>  // static int steps__czet0
STEP<20>  // static int steps__czet1  (el suyo propio)
```

**Nunca** se renombra:
- lo capturado del llamador (los `$param` de la llamada);
- nombres usados *por referencia*: `printf`, tags de `struct`/`union`/`enum`,
  alias de `typedef`;
- el nombre y los parámetros de una función que genera el cuerpo (el
  "contrato" de la macro).

**Gotcha importante:** los *campos* y *variables* que declara el cuerpo
también se renombran. Si el código exterior tiene que verlos, pasa su nombre
como `$param: ident` desde la llamada (provenance = no se renombra), o declara
el tipo fuera de la macro (ver PORTS en §7).

## 5. ¿Se pueden usar genéricos en las macros?

**Sí, se componen.** Una macro define *sintaxis*; los genéricos de CZet
(`T f<T>(...)`, `struct box<T,U>`) definen *abstracción de tipos*. La macro no
es genérica en sí (no existe `_Macro FOO<T>`), pero:

- el fragmento `$t: type` captura un tipo, y el cuerpo puede **declarar e
  instanciar** genéricos con él:

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

**Y muchas veces no hacen falta genéricos.** C tipifica solo: captura el tipo
con `$t: type` y pégalo; el mismo cuerpo sirve para `int`, `double` o un
`struct` (SWAP, §7). Para "gestionar múltiples tipos" también tienes **brazos**
con tokens literales (KIND, LOG) y, si es sobrecarga de operadores, el
`_Operator` de CZet. Los genéricos brillan cuando necesitas *síntesis*:
una nueva función/struct monomorfizado por cada tipo.

## 6. Reglas de sintaxis

1. La definición se cierra con `;` tras el `}` final.
2. Dentro de `<...>` los `<`/`>` anidados y `<<`/`>>` se manejan; los
   `()`, `[]`, `{}` protegen su interior.
3. La llamada debe encajar con el patrón **token a token** (orden y comas).
4. Expansión recursiva limitada a 200 niveles
   (`macro expansion nested too deeply`).
5. **Limitación:** una expansión cuyo primer token sea `(` no funciona en
   contexto de expresión (el `(` inyectado confunde al parser). Empieza con
   una keyword (`int`, `if`, `sizeof`…) o, si quieres un literal compuesto,
   envuélvelo: `sizeof((int[]){ ... })` (ver VEC_LEN en la demo).

## 7. Ejemplos completos (todos verificados)

### 7.1 `LIST` — estilo `vec!`, genera un array

```c
_Macro LIST<$t: ident, $( $x: expr ),*> {
  (LIST $t , $($x),*) { int $t[] = { $($x),* }; }
};

int main(void) {
  LIST<cnt, 3, 1, 4, 1, 5>
  printf("%d\n", cnt[2]);   // 4
}
```

Esta forma (ident + grupo) exige que el patrón escriba la coma tras `$t` y
que la llamada la lleve: `LIST<cnt, 3, 1, 4, 1, 5>`.

### 7.2 `LOG` — variantes por "palabra" (nivel)

```c
_Macro LOG<$lvl: ident, $m: string> {
  (LOG error $m) { fprintf(stderr, "[E] %s\n", $m); }
  (LOG warn  $m) { fprintf(stderr, "[W] %s\n", $m); }
  (LOG info  $m) { printf("[I] %s\n", $m); }
};

LOG<info "listo">
LOG<error "fallo">
```

Fíjate: cada brazo puede usar **subconjuntos** de los parámetros declarados.

### 7.3 `KIND` — brazos por nombre de tipo + fallback

```c
_Macro KIND<$t: ident> {
  (KIND int)    { printf("  entero\n"); }
  (KIND double) { printf("  flotante\n"); }
  (KIND $t)     { printf("  otro tipo\n"); }
};

KIND<int>
KIND<double>
KIND<foo>
```

### 7.4 `PORTS` — un mini-DSL (repeticiones paralelas + higiene)

```c
struct row { const char *key; int value; };   /* tipo declarado FUERA */

_Macro PORTS<$t: ident, $( $name: string ),* , $( $port: number ),*> {
  (PORTS $t $( $name ),* ; $( $port ),*) {
    static const struct row $t[] = { $( { $name, $port } ),* };
  }
};

PORTS<rows "http", "https", "dns" ; 80, 443, 53>
// → static const struct row rows[] = { { "http", 80 },
//                                     { "https", 443 }, { "dns", 53 } };
```

- `struct row` se declara fuera para que la higiene no renombre los campos.
- `$t` (el nombre de la tabla) viene del llamador → no se renombra.
- Las dos repeticiones paralelas se emparejan por índice en la expansión.

### 7.5 `SWAP` — sin genéricos, uno sirve para todos

```c
_Macro SWAP<$t: type, $a: ident, $b: ident> {
  (SWAP $t , $a , $b) { $t __tmp; __tmp = $a; $a = $b; $b = __tmp; }
};

SWAP<int, x, y>
SWAP<double, d1, d2>
```

El mismo cuerpo tipa `int`, `double`, structs… C lo resuelve; no necesitas
monomorfización.

### 7.6 `BOX` / `SUM3` — composición con genéricos (ver §5)

### 7.7 Idea para extensiones típicas de Rust llevadas a C

| Rust | CZet |
|---|---|
| `vec![a, b, c]` | `LIST<xs, a, b, c>` (array), `VEC_LEN<...>` (cuenta) |
| `assert!(x)` / `assert_eq!(a, b)` | `ASSERT_TRUE<a>` / `ASSERT_EQ<a, b>` (demo) |
| `println!("{}", x)` | `PRINT_VALS<...>` / `LOG<lvl "msg">` |
| `todo!()` / `unreachable!()` | macro `$t: tokens` que emite el call |
| tablas estáticas (p. ej. `lazy_static`) | `PORTS<...>` / `TABLE<...>` |
| `match` de formas | brazos: `(DIAG $a)` vs `(DIAG $a , $b)` |

## 8. Compilar

```text
czet -static tests/macro_rules.slt -o macro_rules
./macro_rules
   → Demo completa terminada.
```

`tests/macro_rules.slt` es la referencia sección por sección (1–2 básicos,
3–5 fragmentos, 6–10 repeticiones, 11 higiene, 12 provenance, 13 brazos).