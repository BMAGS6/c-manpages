title: c-storage-duration
section: 7
source: C Manual Pages

# NAME

c-storage-duration -- storage duration and object lifetime in C

# DESCRIPTION

**Storage duration** is a property of an object in C that determines its
**lifetime**: the portion of program execution during which storage is
guaranteed to be reserved for that object.

C defines four storage durations: **automatic**, **static**, **thread**, and
**allocated**. Each describes the circumstances under which an object exists
and when its lifetime ends.

Storage duration is distinct from **scope** and **linkage**. Scope concerns
where an identifier is visible in source code; linkage concerns whether
declarations refer to the same entity. Neither should be confused with an
object's lifetime.

# OVERVIEW

The four storage durations are:

- **Automatic storage duration:** Ordinarily associated with block-scope
  objects and function parameters. Such objects exist for an execution of
  the associated block; variable-length arrays have a different lifetime
  rule tied to execution of their declarations.
- **Static storage duration:** The object's lifetime is the entire execution
  of the program. File-scope objects and block-scope objects declared with
  `static` are common examples.
- **Thread storage duration (since C11):** A distinct object is associated
  with each thread, and its lifetime is the execution of that thread.
- **Allocated storage duration:** Storage is obtained and released through
  allocation operations, such as `malloc` and `free`. Its lifetime is
  governed by those operations rather than by entry into or exit from a
  source-code block.

An object's storage duration is independent of the storage duration of
any pointer object that refers to it. For example, a block-scope pointer
can have automatic storage duration while the object it points to has
allocated storage duration.

The language does not require an automatic object to reside on a hardware
stack, or a static object to occupy any particular executable-file section.
Storage-duration rules specify language-level behavior, not a mandatory
physical memory layout.

For the related storage-class specifier, see the
[`static` keyword](../keywords/static.md).

# SEMANTICS

## Automatic Storage Duration

An object has **automatic storage duration** when its identifier is declared
with no linkage and without the `static` storage-class specifier, subject to
the rules for compound literals and other special cases. Function parameters
and ordinary block-scope objects without `static`, `extern`,
or thread-storage specifiers are common examples.

Function parameters always have automatic storage duration. The only
storage-class specifier permitted in a parameter declaration is `register`,
which does not change the parameter object's automatic storage duration.
For ordinary block-scope object declarations, automatic storage duration is
the default unless a different storage duration is specified.

In ordinary block-scope declarations, the `auto` and `register`
storage-class specifiers specify automatic storage duration. Although
neither changes the default storage duration in these contexts,
`register` additionally restricts address-taking and certain other
operations.

Since C23, `auto` can also request type inference from an initializer.
When combined with another storage-class specifier, or when used at
file scope, `auto` does not itself determine storage duration or linkage.

An automatic object is associated with an execution of a block. Normally, a
new instance is created each time the block is entered, and that instance's
lifetime ends when execution of the block ends. Variable-length arrays have
different lifetime boundaries, described below.

### Ordinary Block-Scope Objects

For an automatic object that does not have a variable-length array type,
its lifetime extends from entry into the associated block until execution
of that block ends. Entering an inner block or calling another function
suspends execution of the outer block; it does not end the outer block's
execution or the lifetimes of its automatic objects.

```c
void example(void)
{                       /* Begin lifetime of `outer` */
    int outer = 10;

    {                     /* Begin lifetime of `inner` */
        int inner = 20;
        outer += inner;
    }                     /* `inner` is no longer alive at this point */

    outer++;
}                       /* `outer` is no longer alive at this point */
```

Here, `outer` exists throughout the current execution of the function body.
The object `inner` exists only during execution of the enclosed block.
After the inner block ends, `outer` is still alive.

**Lifetime and initialization are distinct events.** The lifetime of an
ordinary non-VLA automatic object begins upon entry into its associated
block, even if its declaration occurs later in the block. Its initializer,
if any, is ordinarily evaluated each time execution reaches that declaration.
An automatic object without an initializer has an *indeterminate* initial
representation; its value must not be assumed to be zero. Reading an
uninitialized object's value can result in undefined behavior, depending
on the type of the object and the operation performed. Merely taking the
address of an uninitialized object, where permitted, does not read its
stored value.

Since C23, a block-scope `constexpr` object without `static` also has
automatic storage duration. Its initializer is evaluated at translation
time, and each instance is initialized to the resulting fixed value when
the declaration is reached.

### Function Parameters and Recursion

Function parameters have automatic storage duration and no linkage, akin to
normal block-scope objects. Each invocation of a function has its own parameter
objects and its own instances of automatic objects associated with that
invocation's blocks.

```c
#include <stdio.h>

void countdown(unsigned int depth)
{
    unsigned int current = depth;

    printf("%u\n", current);

    if (depth > 0)
        countdown(depth - 1);
}
```

Each recursive call creates a distinct `current` object. While a recursive
call executes, the caller's block is suspended, not terminated; its automatic
objects remain alive until that call's execution of the block ends.

### Variable-Length Arrays (Since C99)

An automatic object with a **variable-length array** (VLA) type follows a
different rule: its lifetime begins when its declaration is reached and
ends when execution leaves the scope of that declaration. Leaving the
innermost containing block or jumping back to a point before the declaration
can end that lifetime.

```c
#include <stddef.h>

int first_value(size_t count)
{
    if (count == 0)
        return 0;      /* No VLA is created for a zero bound. */

    int values[count]; /* Lifetime of `values` begins here. */

    values[0] = 42;    /* The array is alive and can be modified. */

    return values[0];  /* Return reads the element before its lifetime ends. */
}                      /* Returning ends the lifetime of `values`. */
```

The bound must satisfy the constraints on VLA sizes whenever the declaration
is evaluated. This example returns for a zero bound; it does not attempt to
handle every possible resource limit of the implementation.

### Compound Literals and Temporary Lifetime

Since C99, a compound literal appearing in a block ordinarily provides an
unnamed object with automatic storage duration. Unlike an object with the
special temporary lifetime described below, the compound-literal object's
lifetime lasts for the current execution of the enclosing block.

```c
void example(void)
{
    int* values = (int[]){1, 2, 3};

    values[0] = 10;
}
```

The pointer object `values` and the unnamed array both have automatic storage
duration, although they are distinct objects. The unnamed array remains alive
through the end of the enclosing block. Since C23, an explicit storage-class
specifier can change the duration of a compound-literal object; see the
[`static` keyword](../keywords/static.md).

A separate, narrower rule gives **temporary lifetime** to an object referred
to by certain non-lvalue expressions of structure or union type containing an
array member. Its lifetime ends when evaluation of the containing full
expression ends. Attempting to modify such a temporary object has undefined
behavior. This special case should not be confused with ordinary block-scope
compound literals.

```c
struct numbers
{
    int items[2];
};

static struct numbers
make_numbers(void)
{
    struct numbers result = {{10, 20}};
    return result;
}

int example(void)
{
    /*
     * Temporary exists through this expression.
     * Its lifetime ends at the semicolon
     */
    int first = make_numbers().items[0];


    /* `saved` has ordinary automatic duration. */
    struct numbers saved = make_numbers();

    /* Valid: `saved` is not a temporary. */
    saved.items[0] = first;

    /* Undefined behavior: modifies a temporary. */
    /* make_numbers().items[0] = 99; */

    return saved.items[0];
}
```

The temporary-lifetime rule was introduced in C11. Unlike an ordinary
automatic object whose lifetime is associated with execution of a block,
a temporary object exists only during evaluation of its containing full
expression. A pointer to such an object must not be used after that
lifetime ends. Copying the returned structure value into a named object
does not extend the temporary's lifetime; it initializes a separate
object with its own storage duration.


## Static Storage Duration

An object has **static storage duration** when its identifier is declared
with external or internal linkage, or with the `static` storage-class
specifier, provided that it does not have thread storage duration.
File-scope objects and block-scope objects declared with `static` are
common examples.

Objects without declared identifiers can also have static storage duration.
These include arrays created by string literals and certain unnamed
objects created by compound literals. Their storage duration follows
the specific rules governing those language constructs.

An object with static storage duration exists throughout the entire
execution of the program and is initialized only once, before program
startup.

Unlike automatic objects, which ordinarily have a separate instance for
each execution of their associated block, an object with static storage
duration retains its identity and last-stored value throughout program
execution.

### File-Scope Objects

Objects defined at file scope ordinarily have static storage duration,
regardless of whether their identifiers have external or internal
linkage. Objects declared with a thread-storage specifier are an
exception, having thread storage duration instead.

The `static` storage-class specifier is not required to give an
ordinary file-scope object static storage duration.

```c
/* At file scope: */

/* External linkage; static storage duration. */
int shared_counter = 0;

/* Internal linkage; static storage duration. */
static int private_counter = 0;

void increment(void)
{
    shared_counter++;
    private_counter++;
}
```

Both `shared_counter` and `private_counter` exist throughout program
execution and retain their values between calls to `increment()`.

However, their identifiers have different linkage:

- `shared_counter` has **external linkage**, allowing declarations in
  other translation units to refer to the same object.
- `private_counter` has **internal linkage**, restricting references
  to its identifier to the translation unit in which it is declared.

At file scope, `static` primarily changes the linkage of the declared
identifier. It does not change the storage duration of an ordinary
file-scope object, which would already be static.

An object declared with `extern` may also have static storage duration.
The `extern` declaration does not necessarily define a new object; it
can refer to an object defined elsewhere.

Since C23, a file-scope object declared with `constexpr` also has static
storage duration, and its identifier has internal linkage.

The `static` storage-class specifier can also appear in file-scope
function declarations, where it gives the function identifier internal
linkage. However, functions are not objects and do not have storage
duration. The storage-duration rules described here apply to objects,
not functions.

### Block-Scope Static Objects

An object declared with `static` at block scope has static storage
duration, but its identifier ordinarily has **no linkage**.

Although the identifier is visible only within its scope, the object's
lifetime extends throughout program execution. Leaving the block does
not destroy the object, and entering the block again does not create
a new instance.

```c
unsigned int next_count(void)
{
    /* Initialized once, before program startup. */
    static unsigned int count = 0;

    count++;        /* The previous value is retained. */

    return count;   /* `count` remains alive after returning. */
}
```

The first call to `next_count()` returns `1`, the second returns `2`,
and the third returns `3`. Each invocation accesses the same `count`
object. If incrementing the unsigned counter would exceed its maximum
representable value, the result wraps around according to the rules
of unsigned arithmetic.

Unlike an ordinary automatic object, `count` is not reinitialized every
time execution reaches its declaration. Its initialization occurs once,
before program startup, even if the function is never called.

Because the object remains alive after the function returns, a pointer
to it can remain valid beyond that function invocation:

```c
int* counter_address(void)
{
    static int counter = 0;       /* Lifetime is the entire program execution. */

    counter++;

    return &counter;              /* Valid even after this function returns. */
}
```

The pointer returned by `counter_address()` designates the same object
on successive calls. This differs from returning the address of an
ordinary automatic object whose lifetime ends upon leaving its
associated block.

An object has a **constant address** throughout its lifetime.
Consequently, pointers to the same static object obtained at different
times compare equal, even if the object's stored value changes between
those evaluations.

The following example uses `counter_address()` defined above:

```c
#include <assert.h>

int* counter_address(void);

void verify_counter_address(void)
{
    int* first = counter_address();
    int* second = counter_address();

    assert(first == second); /* Both point to the same static object. */
}
```

Both calls return the address of the same `counter` object. Its value
changes with each call, but its identity and address remain unchanged.
The constant-address guarantee applies within a program execution;
the object need not occupy the same address in separate executions.

Static storage duration does not, by itself, provide synchronization
between threads. Concurrent access to a shared, modifiable static
object must obey C's rules for data races and synchronization.

An object has a **constant address** throughout its lifetime.
Consequently, pointers to the same static object obtained at different
times compare equal, even if the object's stored value changes between
those evaluations.

The following example uses `counter_address()` defined above:

```c
#include <assert.h>

void verify_counter_address(void)
{
    int* first  = counter_address();
    int* second = counter_address();

    assert(first == second);    /* Both point to the same static object. */
}
```

Both calls return the address of the same counter object. Its value
changes with each call, but its identity and address remain unchanged.
The constant-address guarantee applies within a program execution;
the object need not occupy the same address in separate executions.

### Initialization and Default Values

Objects with static storage duration are initialized only once, before
program startup. This requirement applies regardless of whether the
object is declared at file scope or block scope.

If no explicit initializer is provided, the object undergoes default
initialization according to its type.

```c
static int attempts;          /* Initialized to 0. */
static double rate;           /* Initialized to positive zero. */
static int* selected;         /* Initialized to a null pointer. */
static int values[3];         /* Every element is initialized to 0. */
```

Default initialization recursively initializes members of structures
and elements of arrays. For unions, the first named member is
initialized according to the applicable rules.

A null pointer is not necessarily represented by an all-bits-zero
pattern. Default initialization guarantees a null pointer value,
regardless of its underlying representation.

Explicit initialization of an object with static storage duration
is subject to restrictions on initializer expressions. The expressions appearing
in such initializers must be constant expressions or string literals, subject
to the language's initialization constraints.

```c
int compute_value(void);

void example(void)
{
    static int first = 42;        /* Valid: constant initializer. */

    first++;                      /* Later modifications are permitted. */

    /* Invalid: a function call is not a constant expression here. */
    /* static int second = compute_value(); */
}
```

Unlike an ordinary automatic object, a static object cannot generally
be initialized from a value calculated by an arbitrary function call
during program execution. Such a value may instead be assigned to the
object after program startup.

Since C23, an empty initializer (`{}`) can also explicitly request
default initialization.

### Static Compound Literals

Since C99, a compound literal associated with file scope ordinarily
provides an unnamed object with static storage duration. Its lifetime
extends throughout program execution.

```c
/* C99 and later; at file scope. */

int* values = (int[]){10, 20, 30};

/*
 * The unnamed array has static storage duration.
 * The pointer object `values` also has static storage duration.
 */
```

Since C23, a compound literal associated with block scope can also
explicitly specify `static`, giving its unnamed object static storage
duration rather than the automatic storage duration normally associated
with a block-scope compound literal.

```c
/* C23 */

int* persistent_value(void)
{
    return &(static int){42};  /* Unnamed object has static storage duration. */
}
```

The compound literal provides an unnamed `int` object initialized to
`42`. Its lifetime extends throughout program execution, so the
pointer returned by `persistent_value()` remains valid after the
function returns.

The unnamed object is initialized once, before program startup, rather
than being reinitialized on each evaluation of the compound literal.

By contrast, a compound literal without `static` appearing within a
block ordinarily has automatic storage duration, and its unnamed
object's lifetime is associated with execution of that block.

For additional information about the storage-class specifier and
its behavior in different contexts, see the
[`static` keyword](../keywords/static.md).

### String Literals

A string literal provides an unnamed array object with static storage
duration. Its lifetime extends throughout program execution, regardless
of where the string literal appears in the source code.

```c
const char* message(void)
{
    return "Hello, world!"; /* The unnamed array has static storage duration. */
}
```

The pointer returned by `message()` remains valid after the function
returns because the array created by the string literal continues to
exist. This differs from returning a pointer to an ordinary automatic
array declared within the function.

The language does not guarantee that identical string literals designate
distinct arrays. An implementation may share their storage.

Attempting to modify an array created by a string literal results in
undefined behavior, even though ordinary character string literals in C
do not themselves have const-qualified array type.

### Relationship to Thread Storage Duration

Since C11, the thread-storage specifier `_Thread_local` can appear
together with `static`. C23 introduces the `thread_local` keyword
spelling.

An object declared with both `static` and a thread-storage specifier
has **thread storage duration**, not static storage duration.

The presence of `static` does not override the thread-storage
specifier. Such objects have lifetimes associated with individual
threads, as discussed in the following section.
