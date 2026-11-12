title: c-static
section: 7
source: C Manual Pages

# NAME

c-static -- the `static` keyword and storage-class specifier in C

# DESCRIPTION

`static` is a keyword and storage-class specifier in C whose meaning depends on
its context. It can give an identifier **internal linkage**, give an object
**static storage duration**, specify the storage duration of a **compound
literal**, or establish a **minimum accessible array size in a function
parameter declaration**.

# OVERVIEW

In declarations -- and, since C23, in compound literals -- `static` acts as a
**storage-class specifier**. Its effect depends on where it appears.

At file scope, `static` gives an object or function identifier **internal
linkage**, restricting references through that identifier to the same
translation unit. At block scope, `static` ordinarily gives an object **static
storage duration**, so that its value persists between executions of the block.
A block-scope object declared with `static` has **no linkage**.

When combined with `thread_local` (or `_Thread_local` in C11 and C17), `static`
does not give an object static storage duration; the object instead has **thread
storage duration**.

Since C23, `static` may also appear in a compound literal, giving its unnamed
object static storage duration.

A separate use occurs within array declarators in function parameter lists.
There, `static` specifies that the corresponding argument must provide access to
the first element of an array containing **at least the specified number of
elements**. It does **not** act as a storage-class specifier in that context.

# SYNTAX

## File Scope

```c
/* Object with internal linkage and static storage duration. */
static int object = 1;

/* Function with internal linkage. */
static int foo(int x);
```

## Block Scope

```c
void example(void)
{
    /* Object with automatic storage duration and no linkage. */
    int x;

    /* Object with static storage duration and no linkage. */
    static int y;
}
```

## Compound Literals

Since C23, `static` may be used as a storage-class specifier in a compound
literal.

```c
void example(void)
{
    /* Unnamed int object with static storage duration. */
    int* p = &(static int){42};
}
```

## Array Parameters

```c
/* The argument must provide access to at least 16 int elements. */
int bar(int values[static 16]);
```

# SEMANTICS

## File-Scope Declarations

When `static` appears in the declaration of an object or function at file scope,
the declared identifier has **internal linkage**. Declarations of that
identifier within the same translation unit may refer to the same entity,
subject to the usual rules of scope and redeclaration. Declarations in other
translation units cannot refer to that entity through the internally linked
identifier.

```c
static int object;

static void function(void)
{
    /* ... */
}
```

Both `object` and `function` have internal linkage. They may be used elsewhere
within the same translation unit where their declarations are visible. Internal
linkage restricts the use of the identifier, not necessarily access to the
object through a pointer supplied by another function.

File-scope objects not declared with `thread_local` already have static storage
duration, regardless of whether `static` is present. At file scope, the
significant effect of `static` is therefore on linkage.

```c
/* At file scope: */

int external_object;        /* External linkage, static storage duration. */
static int internal_object; /* Internal linkage, static storage duration. */
```

When combined with `thread_local` (or `_Thread_local` in C11 and C17), `static`
gives the file-scope identifier internal linkage, while the object has thread
storage duration.

## Block-Scope Objects

When `static` appears in the declaration of an object at block scope, the object
has **static storage duration**, unless `thread_local` is also specified. An
object with static storage duration exists throughout program execution, rather
than being created anew each time execution enters its block.

```c
void example(void)
{
    int automatic_object = 0;
    static int static_object = 0;

    automatic_object++;
    static_object++;
}
```

Each call to `example` creates a new instance of `automatic_object`; its
lifetime ends when execution leaves the block. In contrast, `static_object`
denotes the same object on every call, and its stored value persists.

A block-scope object declared with `static` has **no linkage**. The keyword
changes the object's storage duration, not its linkage.

When `static` is combined with `thread_local` (or `_Thread_local` in C11 and
C17), the block-scope object instead has **thread storage duration**, with a
separate instance for each thread.

An object with static storage duration is initialized once, before program
startup. If no initializer is supplied, it is initialized according to C's
default initialization rules. An explicit initializer must satisfy the
requirements for initializing an object with static storage duration; an
arbitrary runtime function call is not generally permitted.

For more information, see `c-storage-duration(7)` and `c-initialization(7)`.

## Compound Literals

Since C23, `static` may appear as a storage-class specifier in a compound
literal. The resulting unnamed object has **static storage duration**.

```c
void example(void)
{
    int* pointer_to_automatic = &(int){1};
    int* pointer_to_static    = &(static int){2};
}
```

The unnamed `int` object designated by `(int){1}` has automatic storage duration
because the compound literal occurs within a block. Its lifetime lasts for the
current execution of the enclosing block.

The unnamed object designated by `(static int){2}` instead has static storage
duration and persists throughout program execution. Its initializer is subject
to the rules for static initialization.

Both _pointer variables_ in this example have automatic storage duration;
`static` applies to the unnamed object designated by the second compound
literal, not to `pointer_to_static` itself.

## Array Parameters

When `static` appears between the brackets of an array parameter declarator, it
specifies the minimum number of array elements to which the argument must
provide access on each call. It is not a storage-class specifier in this
context.

```c
void example(int values[static 16]);
```

As with other array parameters, the declaration is adjusted to a pointer type.
Here, `values` is a pointer to `int`; the function does not receive an array by
value.

The `static 16` additionally requires the argument to provide access to the
first element of an array containing at least 16 `int` elements.

```c
void example(int values[static 16])
{
    values[0] = 42;
}

void demonstrate(void)
{
    int a[16] = {0};
    int b[32] = {0};
    int c[8]  = {0};

    example(a); /* Valid. */
    example(b); /* Valid. */

    /* Would have undefined behavior: */
    /* example(c); */
}
```

This requirement is a contract on the caller, **not** a guarantee that the
implementation will perform a runtime bounds check. Violating it has undefined
behavior even if the function does not access all the specified elements.

The bound need not be an integer constant expression. It may depend on another
parameter:

```c
#include <stddef.h>

void process(size_t count, int values[static count]);
```

For each call to `process`, `count` must be positive, and `values` must provide
access to at least `count` elements.

# CONSTRAINTS

- A function declaration using `static` as a storage-class specifier must appear
  at file scope. A block-scope function declaration cannot use it.
- An object with static or thread storage duration cannot have a variable length
  array type.
- In an array parameter declarator, `static` is permitted only in the outermost
  array type derivation. Its bound must have integer type and must be positive
  when evaluated.
- A compound literal using `static` must satisfy the applicable constraints on
  storage-class specifiers and the initialization of objects with static storage
  duration.

# IMPLEMENTATION NOTES

The following examples illustrate how particular projects,
operating systems, and compilers use or interpret `static`.
Unless otherwise indicated, these uses rely on standard C semantics. 
Project-specific macros, compiler diagnostics, and linker behavior are not 
themselves requirements of ISO C.

## Linux Kernel: The `at_least` Macro

The Linux kernel uses `at_least` to express the minimum number of elements 
required by an array parameter.

In `include/linux/compiler_types.h`, the macro is defined conditionally:

```c
#ifndef __CHECKER__
#   define at_least static
#else
#   define at_least
#endif
```

This permits declarations such as:

```c
void process(unsigned char data[at_least 16]);
```

Under ordinary compilation, this is equivalent to:

```c
void process(unsigned char data[static 16]);
```

The declaration requires callers to provide access to at least 16 elements. 
The `at_least` spelling makes this requirement more apparent, without changing 
its semantics, especially to those unfamiliar with this somewhat uncommon and
technical usage of the `static` keyword.

When `__CHECKER__` is defined for Linux's Sparse static analyzer,
the macro expands to nothing, avoiding compatibility problems with the 
array-parameter syntax.

The macro is a Linux kernel convention, not an ISO C keyword or a language 
extension. The underlying array-parameter feature has been part of standard C 
since C99 [9].


The `at_least` macro was introduced by Jason A. Donenfeld in November 2025, 
as part of an effort to annotate fixed-size array parameters in Linux kernel 
cryptographic interfaces. The changes were submitted for inclusion in 
Linux 6.19, covering algorithms including SHA-1, SHA-2, ChaCha, Poly1305, 
and Curve25519 [13].

Before introducing the macro, an existing local variable named `at_least` in 
the Intel `iwlwifi` driver was renamed to `min_mode` to avoid a preprocessor 
name collision [14]. This illustrates how introducing a widely visible macro can
require changes to otherwise unrelated code.


## SQLite: Internal Linkage in the Amalgamation

SQLite distributes an amalgamation that combines many source files into a 
single C translation unit.

Its amalgamation generator can define `SQLITE_PRIVATE` as `static`:

```c
#ifndef SQLITE_PRIVATE
#   define SQLITE_PRIVATE static
#endif
```

The generator uses this macro to control the linkage of internal functions 
and objects while keeping public API declarations separate.

The amalgamation generator normally enables this behavior.
It also provides a `--nostatic` option and allows `SQLITE_PRIVATE` to be 
defined differently.

This illustrates how the preprocessor can be used to control linkage across 
different build configurations without changing the underlying C declarations 
manually.

The `SQLITE_PRIVATE` macro is specific to SQLite; its expansion uses the 
ordinary ISO C meaning of `static` [10].

The `SQLITE_PRIVATE` convention was introduced in SQLite 3.4.1, released on 
July 20, 2007. Its release notes specifically record the use of 
`SQLITE_PRIVATE`, defaulting to `static`, for internal functions in the 
amalgamation.

SQLite 3.4.2, released on August 13, 2007, further improved the amalgamation 
generator so that symbols were prefixed with either `SQLITE_PRIVATE` or 
`SQLITE_API`, as appropriate [15, 16].

## FreeBSD: Static Objects and Kernel Linker Sets

FreeBSD uses a kernel initialization framework called `SYSINIT` to register 
initialization routines and arrange their execution during kernel startup.

As documented in the FreeBSD Architecture Handbook, the `SYSINIT` machinery 
creates registration structures declared with `static`. 
These structures are also placed into linker sets through platform-specific 
mechanisms.

The `static` declaration gives the registration object's identifier internal 
linkage. Nevertheless, the linker can collect references to these objects 
into a set that the FreeBSD kernel subsequently processes.

This demonstrates an important distinction: internal linkage restricts 
references through an identifier, but does not prevent an object's address from 
being made available through other mechanisms.

FreeBSD's linker-set mechanism is implementation-specific.
The internal-linkage behavior of `static` is standard C [11].

The `SYSINIT` framework first appeared in FreeBSD 2.2, released in 1997. 
The FreeBSD `SYSINIT(9)` manual credits Terrence Lambert with developing the 
framework.

The framework remains documented in modern FreeBSD releases, illustrating the 
longevity of this approach to kernel initialization and linker-based 
registration [17].

## GCC: Array-Parameter Diagnostics

GCC uses array-parameter declarations to help diagnose potentially invalid 
calls and inconsistent redeclarations.

For example:

```c
void process(int values[static 4]);
```

The declaration requires the argument to provide access to at least four 
`int` elements.

GCC's `-Warray-parameter` option can diagnose inconsistent array-parameter 
declarations. GCC may also use array-bound information when producing warnings 
such as `-Warray-bounds` and `-Wstringop-overflow`.

For example, the following declarations may produce an inconsistency warning:

```c
void process(int values[static 4]);
void process(int values[]);
```

The first declaration specifies an explicit minimum-size contract, 
while the second omits it.

These diagnostics are compiler facilities, not runtime bounds checks. 
ISO C does not require implementations to diagnose every violation of the 
minimum-size contract [12].

The `-Warray-parameter` diagnostic was introduced in GCC 11, first released 
on April 27, 2021.

GCC 11 added dedicated warnings for inconsistent array parameter declarations, 
including declarations using `static` minimum-size bounds. This extended the 
compiler's ability to diagnose incorrect uses of a language feature that had 
existed since C99 [18].

# SEE ALSO

`c-compound-literals(7)`, `c-initialization(7)`, `c-linkage(7)`, `c-scope(7)`,
`c-storage-duration(7)`

# STANDARDS

The traditional declaration uses of `static` are specified by ANSI C89 and later
C standards. The array-parameter use was introduced in C99, and the
compound-literal use in C23.

Relevant clauses of ISO/IEC 9899:2024 (C23) include:

- **6.2.2**   -- Linkages of identifiers
- **6.2.4**   -- Storage durations of objects
- **6.5.3.6** -- Compound literals
- **6.7.2**   -- Storage-class specifiers
- **6.7.7.3** -- Array declarators
- **6.7.7.4** -- Function declarators
- **6.7.11**  -- Initialization

# HISTORY

The concepts underlying `static` predate C. Earlier languages, including ALGOL
60 and PL/I, supported forms of persistent storage. Ken Thompson's B language
distinguished automatic, external, and internal storage, but did not include a
`static` keyword in its documented syntax [1].

The `static` keyword was present during the early development of C at Bell
Laboratories. It appears in Dennis Ritchie's surviving `last1120c` compiler,
which is believed to date from approximately July 1972. The compiler explicitly
registers `static` as a keyword in `c00.c`. Its exact introduction date and
precise early semantics are uncertain [2].

Ritchie's _C Reference Manual_, dated January 15, 1974, documents `static` as a
storage-class specifier [3]. An early edition of the manual also records
implementation differences: PDP-11 UNIX C rejected file-scope `static`
definitions and did not support explicit initialization of local static objects,
while GCOS C supported both [4].

By 1978, the first edition of _The C Programming Language_ documented the
familiar uses of `static` for persistent local objects and file-local objects
and functions [5].

The following milestones summarize the known history, standardization, and
subsequent extensions of `static`:

- **circa July 1972:** Earliest surviving compiler evidence identified here;
  `static` appears in Dennis Ritchie's `last1120c` compiler, in `c00.c`, line 51
  [2].
- **January 15, 1974:** Documented as a storage-class specifier in Ritchie's _C
  Reference Manual_ [3]. Contemporary UNIX and GCOS implementations differed in
  their support for certain uses of `static` [4].
- **1978 (K&R C):** The first edition of _The C Programming Language_ documented
  the familiar uses of `static` for persistent local objects and file-local
  objects and functions [5].
- **1989 (C89):** Standardized the established declaration uses of `static`,
  including internal linkage and static storage duration.
- **1999 (C99):** Introduced `static` within array parameter declarators.
- **2011 (C11):** Introduced `_Thread_local`, which may be combined with
  `static` to give an object thread storage duration.
- **2024 (C23):** Introduced the `thread_local` keyword spelling and permitted
  `static` in compound literals.

# REFERENCES

1. Ken Thompson, _Users' Reference to B_, Bell Laboratories, January 7, 1972.
   https://cm-bell-labs.github.io/who/dmr/kbman.html

2. Dennis M. Ritchie, _Primeval C: Two Very Early Compilers_, including the
   `last1120c` compiler source (circa 1972--1973).
   https://cm-bell-labs.github.io/who/dmr/primevalC.html Source excerpt:
   https://git.hanabi.in/legacy-cc/file/last1120c/c00.c.html#l51

3. Dennis M. Ritchie, _C Reference Manual_, Bell Laboratories Technical
   Memorandum 74-1273-1, January 15, 1974.
   https://cm-bell-labs.github.io/who/dmr/cman74.pdf

4. Dennis M. Ritchie, _C Reference Manual_, Appendix 2, _Implementation
   Peculiarities_, section B.9 and section C.4.
   https://www.bell-labs.com/usr/dmr/www/cman.pdf

5. Brian W. Kernighan and Dennis M. Ritchie, _The C Programming Language_, first
   edition, Prentice-Hall, 1978, section 4.6, _Static Variables_.

6. Dennis M. Ritchie, _The Development of the C Language_, Second History of
   Programming Languages Conference, 1993.
   https://cm-bell-labs.github.io/who/dmr/chist.html

7. Warren Toomey, _The Restoration of Early UNIX Artifacts_, USENIX Annual
   Technical Conference, 2009.
   https://www.usenix.org/legacy/event/usenix09/tech/full_papers/toomey/toomey.pdf

8. ISO/IEC, _Information technology -- Programming languages -- C_, ISO/IEC
   9899:2024. Public working draft N3220:
   https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf

9. Linux kernel developers, `include/linux/compiler_types.h`,
   definition and documentation of the `at_least` macro.
   https://github.com/torvalds/linux/blob/master/include/linux/compiler_types.h

10. SQLite developers, `tool/mksqlite3c.tcl`, SQLite
    amalgamation generator and `SQLITE_PRIVATE` handling.
    https://github.com/sqlite/sqlite/blob/master/tool/mksqlite3c.tcl

11. FreeBSD Project, _FreeBSD Architecture Handbook_,
    chapter 1, _Bootstrapping and Kernel Initialization_,
    section 1.9, _Kernel Initialization_.
    https://docs.freebsd.org/en/books/arch-handbook/boot/

12. GNU Project, _Using the GNU Compiler Collection_,
    _Warning Options_, `-Warray-parameter`.
    https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html

13. Jason A. Donenfeld, _compiler: introduce at_least
    parameter decoration pseudo keyword_, Linux kernel
    mailing list, November 23, 2025.
    https://lists.openwall.net/linux-kernel/2025/11/23/61

14. Jason A. Donenfeld, _wifi: iwlwifi: trans: rename
    at_least variable to min_mode_, Linux kernel mailing
    list, November 20, 2025.
    https://lists.openwall.net/linux-kernel/2025/11/20/62

15. SQLite developers, _SQLite Release 3.4.1_,
    July 20, 2007.
    https://www.sqlite.org/releaselog/3_4_1.html

16. SQLite developers, _SQLite Release 3.4.2_,
    August 13, 2007.
    https://www.sqlite.org/releaselog/3_4_2.html

17. FreeBSD Project, _SYSINIT(9) -- Kernel Developer's
    Manual_, HISTORY and AUTHORS.
    https://man.freebsd.org/cgi/man.cgi?query=SYSINIT&sektion=9

18. GNU Project, _GCC 11 Release Series: Changes, New
    Features, and Fixes_, C family changes.
    https://gcc.gnu.org/gcc-11/changes.html
