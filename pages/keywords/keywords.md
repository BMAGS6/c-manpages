title: c-keywords
section: 7
source: C Manual Pages

# NAME

c-keywords -- keywords and reserved spellings of the C programming language

# DESCRIPTION

A **keyword** is a token with a spelling reserved by the C language for use in
its grammar. Keywords form part of declarations, statements, expressions, and
other language constructs. Their spelling is **case-sensitive**.

This page describes the lexical rules governing keywords and catalogs the
keywords specified by **ISO C23** (ISO/IEC 9899:2024). It is an index to more
detailed manual pages, not a replacement for the rules of each construct.

# KEYWORDS AT A GLANCE

The following table lists the **54 primary C23 keywords** alphabetically,
with underscore-prefixed keywords listed last. The categories below give more
context and link to dedicated manuals where available.

| Keyword         | Keyword               | Keyword         |
| :-------------- | :-------------------- | :-------------- |
| `alignas`       | `alignof`             | `auto`          |
| `bool`          | `break`               | `case`          |
| `char`          | `const`               | `constexpr`     |
| `continue`      | `default`             | `do`            |
| `double`        | `else`                | `enum`          |
| `extern`        | `false`               | `float`         |
| `for`           | `goto`                | `if`            |
| `inline`        | `int`                 | `long`          |
| `nullptr`       | `register`            | `restrict`      |
| `return`        | `short`               | `signed`        |
| `sizeof`        | [`static`](static.md) | `static_assert` |
| `struct`        | `switch`              | `thread_local`  |
| `true`          | `typedef`             | `typeof`        |
| `typeof_unqual` | `union`               | `unsigned`      |
| `void`          | `volatile`            | `while`         |
| `_Atomic`       | `_BitInt`             | `_Complex`      |
| `_Decimal128`   | `_Decimal32`          | `_Decimal64`    |
| `_Generic`      | `_Imaginary`          | `_Noreturn`     |

The five obsolescent alternative spellings and the additional keyword families
specified by Annex H are listed separately later in this page.

# OVERVIEW

C distinguishes **keywords** from **identifiers**, **constants**, **string
literals**, and **punctuators**. An identifier names an entity, such as a
variable, function, typedef name, or member; a keyword has a role prescribed
by the language grammar.

```c
int count = 42;
```

Here, `int` is a keyword, `count` is an identifier, `42` is an integer
constant, and `=` and `;` are punctuators. The spelling `int` cannot also be
used as an ordinary variable name in C23.

Keywords are not necessarily operators or complete statements by themselves.
For example, `static` has different effects depending on where it appears;
`if` introduces a selection statement; and `sizeof` participates in an
expression. For their detailed behavior, consult the corresponding manual
page when available.

# LEXICAL RULES

## Recognition and Case Sensitivity

During translation, source text is divided into **preprocessing tokens**.
After preprocessing, the resulting tokens are classified according to the C
grammar. In C23, the spellings listed in 6.4.1 are reserved as keywords in
translation phases 7 and 8, except when used as **attribute tokens**.

Matching is case-sensitive: `while` is a keyword, but `While` is not. A
keyword embedded in a longer identifier is not recognized separately:
`static_count` is an identifier, not `static` followed by `_count`.

## Reservation and Identifiers

An ordinary identifier cannot have the spelling of a keyword in a context
where that token is reserved. The language also reserves **some identifier
spellings** for the implementation, including certain names beginning with
underscores; being reserved as an identifier does **not** make a name a
keyword.

```c
int count = 42;            /* Valid ordinary identifier. */

/* Do not use either of these declarations: */
/* int static = 42; */     /* Invalid: static is a keyword. */
/* int _Example = 42; */   /* Reserved for the implementation. */
```

Unlike `static`, `_Example` is not a keyword. Its spelling is reserved for
any use because an underscore followed by an uppercase letter begins the
identifier. Reservation does not necessarily mean that a compiler will
reject the declaration; it means that portable application code must not
claim the identifier.

C23 makes a narrow exception for attribute tokens in `[[...]]`: a spelling
otherwise used as a keyword may be used as an attribute token. This does not
make that spelling available as a normal variable or function name.

## Preprocessing

Preprocessing precedes the ordinary keyword classification. Directive names
such as `define`, `include`, `if`, and `endif` are recognized in preprocessing
directives; they do not all form part of the C keyword set. In particular,
`define` and `include` are **not** keywords in the language proper, even though
`#define` and `#include` are familiar C syntax.

C23 permits implementations to predefine macros named `alignas`, `alignof`,
`bool`, `static_assert`, `thread_local`, `true`, and `false`, but does not
require them to do so. This allows compatibility with earlier spellings and
header-based definitions.

For the five keywords with obsolescent alternative spellings, their alternative
forms, and `true` and `false`, the spelling in expressions subject to the `#`
(stringification) and `##` (token pasting) preprocessing operators is
**unspecified**. This narrow allowance does not generally permit programs
to redefine arbitrary keywords as macros.

# KEYWORD CATEGORIES

The following groups collectively list the **54 keywords** in the primary
C23 keyword grammar (6.4.1). Groups are for navigation: they are **not**
formal lexical categories, and a keyword may have more than one grammatical
role. A link indicates that this reference has a dedicated manual page.

## Types and Type Construction

Keywords used to specify fundamental, derived, or specialized types:

- **Fundamental and integer types:** `void`, `char`, `short`, `int`, `long`,
  `signed`, `unsigned`, `bool`, `_BitInt`.
- **Floating and complex types:** `float`, `double`, `_Complex`, `_Imaginary`,
  `_Decimal32`, `_Decimal64`, `_Decimal128`.
- **Structure, union, and enumeration types:** `struct`, `union`, `enum`.

`_Imaginary` is reserved for imaginary types. Availability of particular
floating-point types and extensions depends on the implementation and the
applicable specifications; a reserved spelling does not imply that every
implementation supports every corresponding type.

## Type Qualifiers

- `const`, `volatile`, `restrict`, `_Atomic`.

These affect the interpretation or permitted use of qualified types. `_Atomic`
can also occur as an atomic type specifier; its use is not limited to an
ordinary qualifier.

## Declarations, Storage, and Functions

- **Storage-class specifiers and related declarations:** `auto`, `extern`,
  `register`, [**`static`**](static.md), `thread_local`, `typedef`, `constexpr`.
- **Function-related specifiers:** `inline`, `_Noreturn`.
- **Alignment specification:** `alignas`.

Several of these affect [**storage duration**](../concepts/storage-duration.md)
or linkage; others affect type aliases, initialization, or functions. In C23,
`auto` can also request **type inference** for an object definition with an
initializer. The new `constexpr` specifier declares an object whose value is
fixed at translation time; such declarations can introduce **named constants**.
Both features have additional rules beyond this keyword overview.

## Selection, Iteration, and Jumps

- **Selection:** `if`, `else`, `switch`, `case`, `default`.
- **Iteration:** `for`, `while`, `do`.
- **Jumps and loop control:** `goto`, `break`, `continue`, `return`.

These words participate in the syntax and control flow of statements; they
are not ordinary function calls.

## Expressions and Compile-Time Facilities

- **Size, alignment, and type inspection:** `sizeof`, `alignof`, `typeof`,
  `typeof_unqual`.
- **Generic selection:** `_Generic`.
- **Compile-time assertions:** `static_assert`.
- **Predefined constants:** `true`, `false`, `nullptr`.

`true`, `false`, and `nullptr` are keywords naming predefined constants, not
identifiers declared by a standard-library header. Conversely, `NULL`,
`size_t`, `printf`, and `malloc` are **not** keywords.

# ALTERNATIVE SPELLINGS

C23 retains five older spellings as **obsolescent alternative keywords**.
They can be used wherever their primary spelling can, but new C23 code
should normally prefer the primary spelling.

| C23 keyword     | Obsolescent alternative |
| --------------- | ----------------------- |
| `alignas`       | `_Alignas`              |
| `alignof`       | `_Alignof`              |
| `bool`          | `_Bool`                 |
| `static_assert` | `_Static_assert`        |
| `thread_local`  | `_Thread_local`         |

These alternatives are not counted separately among the 54 primary keywords
above. `_Noreturn` is also obsolescent, but it is **not** an alternative
spelling in this table: C23 provides the `[[noreturn]]` attribute instead.

# STANDARD EVOLUTION

The keyword set has grown as C acquired new features:

- **C90:** established the original standardized keyword vocabulary.
- **C99:** added `inline`, `restrict`, `_Bool`, `_Complex`, and `_Imaginary`.
- **C11:** introduced `_Alignas`, `_Alignof`, `_Atomic`, `_Generic`,
  `_Noreturn`, `_Static_assert`, and `_Thread_local`.
- **C23:** added spellings such as `alignas`, `alignof`, `bool`, `static_assert`,
  and `thread_local`, as well as `constexpr`, `typeof`, `typeof_unqual`,
  `_BitInt`, `nullptr`, `true`, and `false`, among other additions.

A spelling that is a keyword in C23 might have been an ordinary identifier or
a library macro in an earlier C version. When writing code intended for
multiple C versions, distinguish the language version from the headers that
may supply compatibility macros.

## The Historical `entry` Keyword

The spelling `entry` appeared among the reserved words of pre-standard C.
An early *C Reference Manual* by Dennis Ritchie described it as reserved for
future use, while explicitly noting that it was not implemented by any
compiler at that time (January, 1974)[4]. It was associated with the possibility
of functions having multiple, differently named entry points, as in Fortran [6].

Kernighan and Ritchie later described `entry` as formerly reserved but never
used in the second edition of *The C Programming Language* (April, 1988)[5].
It was not retained as an ISO C keyword, so `entry` may be used as an ordinary
identifier under the usual identifier rules.

# EXTENSIONS AND RELATED TOKENS

The inventory above is the **primary** C23 keyword list. C23 Annex H, for
implementations supporting its [ISO/IEC 60559](https://www.iso.org/standard/80985.html) interchange and extended
floating-point facilities, expands the keyword grammar with spellings such as
`_Float16`, `_Float32`, `_Float64`, `_Float128`, `_Float32x`, `_Float64x`,
`_Float128x`, `_Decimal64x`, and `_Decimal128x`, including specified
families of additional `_FloatN` and `_DecimalN` spellings. Their supported
types are implementation-dependent.

The commonly encountered `asm` and `fortran` spellings are described among
common extensions in the standard's informative annex; they are **not** in
the primary 6.4.1 keyword list. Compilers may also recognize other
implementation-specific spellings, including extensions beginning with
underscores.

An **attribute token**, a **preprocessing directive name**, a **library
identifier**, and a **keyword** have different roles even if they look alike.
For example, the attribute `[[deprecated]]` does not make `deprecated` a
primary C keyword, and `#include` does not make `include` one.

# SEE ALSO

- [**`c-static(7)`**](static.md) -- the contextual semantics of `static`.
- [**`c-storage-duration(7)`**](../concepts/storage-duration.md) -- object
  lifetimes associated with declarations.

# REFERENCES

1. ISO/IEC 9899:2024, *Programming languages -- C*, 6.4 (Lexical elements),
   6.4.1 (Keywords), 6.4.2 (Identifiers), and 6.10 (Preprocessing directives).
   See the [publicly available WG14 draft N3220](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf).
2. ISO/IEC 9899:2024, Annex H (IEC 60559 floating-point arithmetic), H.5.1
   (Keywords), and Annex J (Common extensions), J.5.9--J.5.10; see N3220.
3. [cppreference: C keywords](https://en.cppreference.com/w/c/keyword).
4. Dennis M. Ritchie, [*C Reference Manual*](https://www.bell-labs.com/usr/dmr/www/cman.pdf),
   "Keywords" -- an early description of the reserved `entry` spelling.
   (January 15, 1974)
5. Brian W. Kernighan and Dennis M. Ritchie, *The C Programming Language*,
   second edition (April, 1988), Appendix A.2.4, "Keywords".
6. Steve Summit, [*comp.lang.c FAQ*, question 20.37](https://www.faqs.org/faqs/C-faq/faq/),
   "What was the entry keyword mentioned in K&R1?"
   (source's last modified date is July 3, 2004, as of citing)
