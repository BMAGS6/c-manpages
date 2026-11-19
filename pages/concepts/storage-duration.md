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

Function parameters *always* have automatic storage duration. The only
storage-class specifier permitted in a parameter declaration is `register`,
which does not change the parameter object's automatic storage duration.
For ordinary block-scope object declarations, automatic storage duration is
the default unless a different storage duration is specified.

In ordinary block-scope declarations, the `auto` and `register`
storage-class specifiers specify automatic storage duration. Although
neither changes the default storage duration in these contexts,
`register` additionally restricts address-taking and certain other
operations.

Since C23, `auto` can also request **type inference** from an initializer.
When combined with another storage-class specifier, or when used at
file scope, `auto` does not, on its own, determine storage duration or linkage.

An automatic object is associated with an execution of a block. Normally, a
new instance is created each time the block is entered, and that instance's
lifetime ends when execution of the block ends. Variable-length arrays have
different lifetime boundaries, described below.

Since C11, the result of attempting to indirectly access an object
with automatic storage duration from a thread other than the one
with which the object is associated is implementation-defined.

This rule is separate from the requirements governing data races
and synchronization. An implementation permitting such access does
not make concurrent, unsynchronized modifications automatically safe.

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

    {                       /* Begin lifetime of `inner` */
        int inner = 20;
        outer += inner;
    }                       /* `inner` is no longer alive at this point */

    outer++;
}                       /* `outer` is no longer alive at this point */

/* Final value of `outer` is 31 */
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
automatic storage duration. Its initializer is evaluated at translation time,
and each instance is initialized to the resulting fixed value when the
declaration is reached.

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

Within a particular execution of the enclosing block, repeated
evaluations of the same compound literal designate the same unnamed
object. However, its initializer is evaluated each time the compound
literal is evaluated, unless a different rule applies.

Entering a new execution of the enclosing block can create a
distinct instance of an automatic compound-literal object.

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
     * `make_numbers()` produces a structure value referring to
     * an object with temporary lifetime.
     * Its array member remains accessible through this full
     * expression, and the temporary's lifetime ends afterward.
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
    static int counter = 0;      /* Lifetime is the entire program execution. */

    counter++;

    return &counter;             /* Valid even after this function returns. */
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
    int* first  = counter_address();
    int* second = counter_address();

    assert(first == second);    /* Both point to the same static object. */
}
```

Both calls return the address of the same `counter` object. Its value
changes with each call, but its identity and address remain unchanged.
The constant-address guarantee applies within a program execution;
the object need not occupy the same address in separate executions.

Static storage duration does not, by itself, provide synchronization
between threads. Concurrent access to a shared, modifiable static
object must obey C's rules for data races and synchronization.

### Initialization and Default Values

Objects with static storage duration are initialized only once, before
program startup. This requirement applies regardless of whether the
object is declared at file scope or block scope.

If no explicit initializer is provided, the object undergoes default
initialization according to its type.

```c
static int    attempts;     /* Initialized to 0. */
static double rate;         /* Initialized to positive zero. */
static int*   selected;     /* Initialized to a null pointer. */
static int    values[3];    /* Every element is initialized to 0. */
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

## Thread Storage Duration

Introduced in C11, **thread storage duration** associates an object with a
particular thread of execution. An object with thread storage duration exists
for the entire execution of its associated thread, rather than for the entire
execution of the program.

Each thread has its own distinct instance of the object. Accessing the object
through its declared identifier refers to the instance associated with the
thread evaluating the expression.

Unlike an ordinary automatic object, a thread-local object is not created anew
whenever execution enters a block or calls a function.
Unlike an object with static storage duration, however, its lifetime is limited
to the execution of its associated thread.

Thread storage duration is specified using the storage-class specifier keyword
`_Thread_local` in C11 and C17, or `thread_local` in C23.

### Declaration and Storage-Class Specifiers

C11 introduced the keyword `_Thread_local` as a storage-class specifier that
gives an object thread storage duration.

```c
/* C11 and later. */
static _Thread_local unsigned int counter = 0;
```

C23 introduced the `thread_local` keyword as the preferred spelling.
The earlier `_Thread_local` spelling remains available for
compatibility, but is deprecated in C23.

```c
/* C23 */
static thread_local unsigned int counter = 0;
```

In C11 and C17, `<threads.h>` also defines `thread_local` as a convenience
macro expanding to `_Thread_local`. Since C23, `thread_local` is a language
keyword itself and no longer requires the header.

The thread-storage specifier may appear alone or alongside `static` or `extern`,
subject to the rules governing its declaration context.

For example, at file scope:

```c
/* C23; at file scope. */

/* External linkage; thread storage duration. */
thread_local int shared_counter = 0;

/* Internal linkage; thread storage duration. */
static thread_local int private_counter = 0;
```

At block scope, `thread_local` must appear with either `static` or `extern`.
It cannot appear by itself.

```c
void example(void)
{
    /* Thread storage duration; no linkage. */
    static thread_local unsigned int calls = 0;

    calls++;
}
```

Here, `calls` has block scope and no linkage, but each thread has a
**separate instance** that persists between calls to `example()`.

A thread-storage specifier applies to **objects only**, not functions.
It cannot be used to give a function thread storage duration.

### Per-Thread Object Instances

An object with thread storage duration has a **separate instance** for each
thread. Modifications made to one thread's instance do not modify the
corresponding instances associated with other threads.

```c
/* C11 */

#include <stddef.h>
#include <threads.h>

static _Thread_local unsigned int counter = 0;

static unsigned int next_count(void)
{
    return ++counter;
}

static int worker(void* argument)
{
    (void)argument;

    next_count();
    return (int)next_count();
}

int main(void)
{
    thrd_t thread;
    int worker_result;

    if (thrd_create(&thread, worker, NULL) != thrd_success)
        return 1;

    unsigned int main_result = next_count();

    if (thrd_join(thread, &worker_result) != thrd_success)
        return 1;

    return (main_result == 1 && worker_result == 2) ? 0 : 1;
}
```

The worker thread calls `next_count()` twice, incrementing its own `counter`
object from zero to two.

The main thread calls the same function, but accesses a different instance of
`counter`. Its first call therefore returns one, regardless of how many times
the worker has incremented its own instance.

Both threads use the same function and the same identifier, but their accesses
designate distinct objects.

The example uses the C11 threads library, which may be unavailable on some
implementations. Thread storage duration itself is a language feature and does
not require threads to be created specifically through `<threads.h>`.

### Lifetime and Initialization

The lifetime of an object with thread storage duration is the entire execution
of the thread for which it is created.

Each thread's instance receives its initial stored value when that thread
starts. The instance retains its identity and last-stored value throughout the
execution of that thread, unless the program modifies it.

```c
/* C23 */

unsigned int next_count(void)
{
    static thread_local unsigned int count = 0;

    return ++count;
}
```

For any particular thread, successive calls to `next_count()` operate on the
same `count` object. The object is not reinitialized each time execution enters
the function.

A different thread has its own instance, initialized to zero when that thread
starts.

Initialization follows the applicable rules for objects with thread storage
duration. An object without an explicit initializer receives default
initialization according to its type.

An object with thread storage duration also has a constant address throughout
its lifetime, just like objects with other storage durations.

Within a particular thread, obtaining the address of the same thread-local
object at different times produces pointers that compare equal. The object's
address remains stable even as its stored value changes.

Different threads, however, have distinct instances of the object.
The constant-address guarantee applies to each instance during its associated
thread's execution.

```c
/* C23 */

static thread_local int  attempts;      /* Initialized to 0. */
static thread_local int* current;       /* Initialized to a null pointer. */
static thread_local int  values[4];     /* Elements initialized to 0. */
```

In C23, the explicit or implicit initializer is evaluated before program
execution, and its previously determined value is used to initialize each
thread's instance when the thread starts.

This distinction is important: an initializer is not an arbitrary expression
evaluated separately whenever a new thread first accesses the object.

As with static initialization, the initializer must satisfy the language's
restrictions on initialization expressions.

When a thread's execution ends, the lifetimes of its thread-local objects end.
A pointer to such an object must not be used after that lifetime has ended.

Thread-local storage does not require explicit deallocation through `free()`.

### Scope and Linkage

Storage duration, scope, and linkage remain distinct properties of a
thread-local object.

The thread-storage specifier determines the object's storage duration.
Other aspects of its declaration determine where its identifier is visible and
whether declarations refer to the same entity.

At file scope, `static thread_local` gives the identifier internal linkage,
while `thread_local` without `static` ordinarily gives it external linkage.

At block scope, `static thread_local` declares an object with thread storage
duration and no linkage.

An `extern thread_local` declaration can refer to a thread-local object with
linkage, subject to the ordinary linkage and redeclaration rules.

Every declaration of the same thread-local object must include the
thread-storage specifier.

Linkage does not determine whether different threads share an object instance.
An identifier may have external linkage while still referring to a distinct
object in each thread.

Similarly, an identifier with block scope and no linkage can designate an object
whose lifetime lasts for the entire execution of its associated thread.

### Access from Other Threads

Ordinarily, a thread accesses its own instance of a thread-local object by using
the object's declared identifier.

A thread can obtain a pointer to its own thread-local object and pass that
pointer to another thread. However, possession of such a pointer does not
guarantee that the receiving thread can portably access the object it
designates.

The C standard specifies that the result of attempting to
**indirectly access** an object with thread storage duration from another
thread is **implementation-defined**.

Consequently, a program cannot assume that dereferencing a pointer to another
thread's thread-local object has the same semantics on every conforming
implementation.

It is also distinct from accessing an object after its lifetime has ended.

If a thread has terminated, the lifetimes of its thread-local objects have
ended. Using a pointer value to one of those expired objects in an evaluation
has undefined behavior.

### Thread Storage and Synchronization

Thread storage duration does not, by itself, provide a general-purpose
synchronization mechanism.

Separate threads can modify their own thread-local instances without introducing
a data race between those instances, because they are distinct objects.

However, using thread-local storage does not make other objects accessed by the
same function thread-safe.

For example, a function may modify both a thread-local counter and a shared
object with static storage duration. The thread-local counter does not protect
accesses to the shared object.

Concurrent access to shared, non-atomic objects must still obey C's rules
governing data races and synchronization.

Thread-local storage is therefore useful for maintaining per-thread state,
but is not a substitute for mutexes, atomic operations, or other synchronization
facilities.

The C threads library also provides a separate mechanism called
**thread-specific storage**, through functions such as `tss_create()`,
`tss_get()`, and `tss_set()`. These functions associate per-thread pointer
values with runtime-managed keys, rather than declaring objects with the
`thread_local` storage-class specifier.

Unlike ordinary thread-local object declarations, this mechanism also supports
registering destructor functions for thread-specific values. The two facilities
serve related purposes but have different interfaces and lifetime-management
rules.

### Thread-Local Compound Literals (Since C23)

Since C23, a compound literal can explicitly specify thread storage duration.

For example:

```c
/* C23 */

int* thread_value(void)
{
    return &(static thread_local int){42};
}
```

The compound literal provides an unnamed `int` object with thread storage
duration. Each thread has its own instance, initialized to `42` when the
thread starts.

The pointer returned by `thread_value()` remains valid after the function
returns, provided that the associated thread's lifetime has not ended.

As with named thread-local objects, indirectly accessing this object from
another thread is implementation-defined.

The use of `static` alongside `thread_local` is required
here because the compound literal occurs at block scope.

This syntax was introduced in C23; compilers that support the `thread_local`
keyword may not yet support storage-class specifiers in compound literals.

## Allocated Storage Duration

**Allocated storage duration** describes the lifetime of an object whose
storage is obtained through a memory allocation operation.

Unlike objects with automatic, static, or thread storage duration,
allocated objects do not have lifetimes determined by the execution of
a block, a thread, or the program as a whole.

Instead, their lifetimes extend from the allocation of their storage
until its deallocation.

ISO C provides memory allocation and deallocation facilities through
the standard library, including `malloc()`, `calloc()`, `realloc()`, and
`free()`, declared in `<stdlib.h>`.

Allocated objects do not require declared identifiers. They are ordinarily
accessed through pointers, which are themselves separate objects with their own
storage durations.

The C standard does not require allocated storage to reside in a particular
physical memory region, such as a conventional heap. Storage duration describes
language-level lifetime guarantees, not a mandatory physical memory layout.

### Allocation and Object Lifetime

The lifetime of an allocated object begins when its storage is successfully
allocated and ends when that storage is deallocated.

An allocated object exists independently of the scope, lifetime, and linkage of
any identifier used to access it.

Like other objects, an allocated object has a constant address throughout its
lifetime. Its identity and lifetime are not determined by the existence of any
particular pointer variable.

Each successful allocation of nonzero size provides storage for an object
distinct from other objects whose lifetimes overlap with it.

The allocated storage can be used to access an object, or an array of objects,
of a suitable type and size, subject to the language's alignment and
effective-type requirements.

An allocated object's lifetime does not end merely because a function returns,
a block is exited, or a pointer variable is destroyed.

### Allocated Objects and Pointer Objects

The storage duration of an allocated object is independent of the storage
duration of a pointer that designates it.

A pointer variable is itself an object, with its own storage duration
and lifetime. The pointer value stored within it may designate another,
separate object whose storage duration and lifetime are independent
of those of the pointer variable.

Consider the following example:

```c
#include <stdlib.h>

int* create_number(void)
{
    int* local_ptr = malloc(sizeof *local_ptr);

    if (local_ptr != NULL)
        *local_ptr = 42;

    return local_ptr;
}

int main(void)
{
    int* caller_ptr = create_number();

    if (caller_ptr == NULL)
        return EXIT_FAILURE;

    int result = *caller_ptr;

    free(caller_ptr);

    return result == 42 ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

Within `create_number()`, `local_ptr` is a pointer object with
**automatic storage duration**. It is a local variable whose lifetime ends when
execution of the function's block finishes.

If `malloc()` succeeds, the pointer value stored in `local_ptr` designates a
separate object with **allocated storage duration**.

The allocated object's lifetime is independent of that of `local_ptr`.
The allocated object remains alive after `create_number()` returns,
even though the lifetime of `local_ptr` has ended.

When the function returns, the pointer value is returned to the caller.
The local pointer object `local_ptr` ceases to exist, but the returned pointer
value allows the caller to continue accessing the allocated object.

In `main()`, `caller_ptr` is another pointer object with automatic storage
duration. It stores the returned pointer value and designates the same allocated
object that was previously accessed through `local_ptr`.

The call to `free()` ends the lifetime of the allocated object, not the lifetime
of `caller_ptr`.

The pointer object `caller_ptr` remains alive until execution leaves
its associated block. However, when the allocated object's lifetime
ends, the representation stored in `caller_ptr` becomes indeterminate.
Using the expired pointer value in an evaluation has undefined behavior.

Thus, the two pointer objects and the allocated object have distinct lifetimes,
even though the pointers designate the same allocation at different times.

This distinction applies regardless of the pointer's own storage duration.
A pointer with static or thread storage duration can also designate an object
with allocated storage duration.

### Initialization and Effective Type

Unlike objects with static or thread storage duration, allocated objects do not
necessarily receive default initialization merely because their storage has been
allocated.

Storage obtained through `malloc()` has an
**indeterminate initial representation**. Its contents **must not** be assumed
to represent initialized values of the types through which it will be accessed.

The standard library also provides `calloc()`, which initializes all bits of the
allocated storage to zero.

However, an all-bits-zero representation is not necessarily equivalent to the
zero value of every C type. In particular, ISO C does not require the
all-bits-zero representation to designate a null pointer
or a floating-point zero.

Allocated objects also differ from ordinary declared objects because they have
**no declared type**.

Instead, the type through which their stored values may be accessed is governed
by the language's **effective-type rules**.

For example:

```c
#include <stdlib.h>

void example(void)
{
    int* value = malloc(sizeof *value);

    if (value != NULL)
    {
        *value = 42;

        free(value);
    }
}
```

The allocation does not declare an object of type `int`.

Rather, the assignment through the `int` lvalue establishes the effective type
of the allocated object for that access and subsequent accesses that do not
modify its stored value.

The effective type of an allocated object may also be affected by copying object
representations, including through operations such as `memcpy()` and
`memmove()`.

The ability to convert a pointer to another pointer type does not, by itself,
permit arbitrary accesses through that type.

Effective type and storage duration describe different properties of an object.
Storage duration determines when an object exists, while effective type
constrains the types of expressions through which its stored value may be
accessed.

The `malloc()` function returns a pointer of type `void*`, which C implicitly
converts to a pointer to an object type when required.

An explicit cast, such as `(int*)malloc(sizeof(int))`, is therefore
unnecessary in C, though it is commonly seen. Such a cast does not change the
allocated object's storage duration, declared-type status, or effective-type
rules.

C++ has different implicit pointer-conversion rules and requires an explicit
conversion in this situation.

### Deallocation and End of Lifetime

The lifetime of an allocated object ends when its storage is deallocated.

For storage obtained through the standard memory allocation functions, this
ordinarily occurs through `free()`, or through a successful `realloc()`
operation.

```c
#include <stdlib.h>

void example(void)
{
    int* ptr_to_int = malloc(sizeof *ptr_to_int);

    if (ptr_to_int != NULL)
    {
        *ptr_to_int = 42;

        free(ptr_to_int);    /* End the allocated object's lifetime. */

        ptr_to_int = NULL;
    }
}
```

After `free()` deallocates the storage, the object formerly designated by
`ptr_to_int` no longer exists.

Passing a null pointer to `free()` has no effect. This differs
from passing a non-null pointer to storage that has already been
deallocated, which results in undefined behavior.

The pointer variable itself continues to exist until its own lifetime ends.
However, the old pointer value designating the deallocated object can no longer
be used.

When an object's lifetime ends, the representations of pointer objects
designating that object, or pointing just beyond it, become indeterminate.

Using such a pointer value in an evaluation has undefined behavior.
This includes attempting to dereference or compare an expired pointer.

Assigning `NULL` to `ptr_to_int` after deallocation replaces the expired pointer
value stored in that particular variable. It does not change other pointers that
may have designated the same object.

Attempting to deallocate the same allocation more than once has undefined
behavior, as does passing an inappropriate pointer to a standard deallocation
function.

If every usable pointer to an allocated object is lost before its storage is
deallocated, the storage may become unreachable. This situation is commonly
called a **memory leak**.

A memory leak does not, by itself, terminate the object's lifetime or deallocate
its storage. Unless the storage is subsequently deallocated, the allocation
remains outstanding for the remainder of program execution.

### Reallocation and Object Identity

Reallocation illustrates an important distinction between an object's identity
and the physical address of its storage.

When `realloc()` succeeds with a valid, non-null pointer and a nonzero requested
size, it ends the lifetime of the old allocated object and provides a new
allocated object.

The new object preserves the contents of the old object up to the smaller of
their allocation sizes.

Any additional bytes beyond the old allocation size have unspecified values.

The new object may occupy a different memory address, or its pointer may have
the same address value as the old allocation.

Nevertheless, **the old object's lifetime has ended**.

Pointers designating the old object are no longer valid for accessing the new
object, even when the returned pointer has the same address value.

For example:

```c
#include <stdlib.h>

void example(void)
{
    int* values = malloc(4 * sizeof *values);

    if (values == NULL)
        return;

    for (int i = 0; i < 4; i++)
        values[i] = i;

    int* resized = realloc(values, 8 * sizeof *values);

    if (resized == NULL)
    {
        free(values);   /* Original object remains alive on failure. */
        return;
    }

    values = resized; /* Use the newly returned pointer. */

    for (int i = 4; i < 8; i++)
        values[i] = 0;

    free(values);
}
```

If reallocation fails, the original object remains alive and its stored contents
are unchanged.

If reallocation succeeds, the original object ceases to exist and the new object
becomes accessible through the returned pointer.

Using a separate pointer variable for the reallocation result ensures that the
original allocation remains accessible if the operation fails.

Since C23, calling `realloc()` with a non-null pointer and a requested size of
zero has undefined behavior.

The **essential lifetime rule** is that successful reallocation does not extend
the lifetime of the original object; it ends that lifetime and begins another.

When the pointer argument is null, `realloc()` behaves like
`malloc()` for the requested size.

Consequently, `realloc(NULL, 0)` follows the implementation-defined
rules for zero-size allocation, while `realloc(ptr, 0)` with a
non-null pointer has undefined behavior in C23.

### Alignment and Allocation Requirements

Allocated storage must satisfy the alignment requirements applicable to the
objects stored within it.

The ordinary standard allocation functions provide storage suitably aligned for
objects with fundamental alignment requirements, subject to the requested
allocation size.

For objects requiring a supported extended alignment, C11 introduced
`aligned_alloc()`.

Alignment determines which types of objects may be suitably accessed at a
particular storage address. It does not determine the storage duration of those
objects.

The amount of storage requested from an allocation function is expressed in
bytes, generally using `size_t`.

A program must ensure that its requested allocation is large enough for the
objects it intends to access.

For example, calculating an allocation size by multiplying an element count by
the size of each element can overflow `size_t`, potentially resulting in an
allocation smaller than intended.

The `calloc()` function specifically detects overflow in the product of its
element-count and element-size arguments and returns a null pointer.

Allocation failure does not create an allocated object.
The standard allocation functions indicate failure by returning a null pointer.

The language does not guarantee that two allocations performed at different
times occupy distinct physical addresses if their lifetimes do not overlap.

Once an allocation has been deallocated, an implementation may reuse its
storage for a subsequent allocation.

That subsequent allocation creates a new object with its own lifetime,
even if the storage occupies the same address.

### Special Cases and Constraints

A request to allocate zero bytes has implementation-defined behavior under the
applicable standard allocation rules.

An implementation may return a null pointer, or behave as though some nonzero
amount of storage were requested. However, a pointer returned for a zero-size
request must not be used to access an object.

This differs from passing a non-null pointer and a zero size to `realloc()`,
which has undefined behavior since C23.

An allocated object must not be accessed outside its lifetime.
The end of an object's lifetime also invalidates the use of pointer values
designating that object.

C23 additionally introduced `free_sized()` and `free_aligned_sized()`,
which permit deallocation using information about the allocation's
requested size and, where applicable, its requested alignment.

The `free_sized()` function accepts a null pointer or a pointer returned by
`malloc()`, `calloc()`, or `realloc()`, together with the corresponding
requested allocation size.

The `free_aligned_sized()` function accepts a null pointer or a
pointer returned by `aligned_alloc()`, together with the corresponding
requested alignment and allocation size.

Passing an incompatible pointer, size, or alignment results in
undefined behavior.

These facilities do not introduce another storage duration or change the
fundamental lifetime rules for allocated objects.

These facilities have additional argument requirements, but do not introduce a
new storage duration or change the fundamental rules governing allocated object
lifetimes.

Finally, ISO C does not mandate a particular allocator implementation,
memory-region layout, or underlying operating-system mechanism.

The term **heap** is commonly used to describe dynamically managed storage,
but the C standard's guarantees concern allocated objects and their lifetimes,
not a particular hardware or operating-system memory organization.


# GENERAL OBJECT LIFETIME RULES

Although C defines four distinct storage durations, several fundamental rules
governing object lifetime apply regardless of an object's storage duration.

An object's **lifetime** is the portion of program execution during which
storage is guaranteed to be reserved for that object. During its lifetime,
an object exists, has a constant address, and retains its last-stored value,
subject to the rules governing initialization and access.

Storage duration determines the circumstances under which an object's lifetime
begins and ends. It does not, by itself, determine the object's type, scope,
linkage, or physical memory location.

## Lifetime and Initialization

The beginning of an object's lifetime and the initialization of its stored value
are distinct concepts.

An object may exist without yet having a determinate stored value.
The fact that storage is reserved for an object does not imply that its
representation contains a valid, initialized value of its type.

For example:

```c
void example(void)
{
    int number;    /* The object's lifetime has already begun. */

    number = 42;   /* Establish a determinate stored value. */
}
```

The lifetime of `number` begins when execution enters its associated block,
even though its declaration is encountered later in the block.

Since `number` has no initializer, its initial representation is indeterminate.
The assignment establishes a stored value that can subsequently be read.

Reading an uninitialized object can result in undefined behavior, depending on
its type and the operation performed.

Objects with other storage durations follow different initialization rules.
Static and thread-local objects receive initialization according to their
respective language requirements, while the initial contents of allocated
objects depend on the allocation operation.

An object's lifetime therefore establishes when the object exists, not
necessarily when its stored value becomes safe to read.

## Object Identity and Address Stability

An object has a **constant address** throughout its lifetime.

This means that pointers to the same object, obtained at different times during
that lifetime, compare equal.

The object's address remains constant even if its stored value changes.

An object also retains its last-stored value throughout its lifetime until that
value is changed by a subsequent store. For a `volatile` object, the last store
need not be explicitly performed by the program.

The constant-address guarantee applies to a particular object's lifetime within
a program execution. It does not require the object to occupy the same address
in separate executions of the program.

Nor does a constant address necessarily imply that an object has an address
unique from every other object that may exist during program execution.

Objects whose lifetimes do not overlap may occupy the same storage at different
times.

An object with temporary lifetime is also a special case: the C standard does
*not* require such an object to have a unique address.

A constant address is therefore not the same as a unique or permanent identity.
A structure and its initial member, when that member is not a bit-field, share
a starting address despite being distinct objects. Likewise, storage occupied
by one object may later be reused by another whose lifetime begins after the
first object's lifetime has ended.

An object's address is stable throughout its lifetime, but the address alone
cannot establish that two expressions designate the same object, or that an
object which previously occupied that address still exists.

## End of Lifetime and Pointer Validity

When an object's lifetime ends, the object ceases to exist.

Referring to an object outside its lifetime results in undefined behavior,
regardless of the storage duration it previously had.

The end of an object's lifetime also affects pointer values that designated
the object.

If a pointer value is used in an evaluation after the lifetime of the object it
points to, or just past, has ended, the behavior is
undefined.

Additionally, the representation of a pointer object becomes indeterminate when
the lifetime of the object it points to, or just past, ends.

This does not mean that the pointer object itself necessarily ceases to exist.
Its lifetime is determined independently.

For example:

```c
#include <stddef.h>

void example(void)
{
    int* ptr = NULL;

    {                       /* Lifetime of `local` begins */
        int local = 42;

        ptr = &local;
    }                       /* Lifetime of `local` ends */

    /*
     * `local` no longer exists.
     * `ptr` still exists, but its stored pointer
     * representation is now indeterminate.
     */

    ptr = NULL; /* Replace the stored value without using it. */
}
```

The pointer object `ptr` has automatic storage duration associated with the
outer block.

The object `local` has automatic storage duration associated with the
inner block.

When execution leaves the inner block, the lifetime of `local` ends.
Consequently, the pointer value previously stored in `ptr` can no longer be
used.

However, `ptr` itself remains alive because execution is still within its
associated block.

Assigning `NULL` to `ptr` replaces its indeterminate pointer representation
without evaluating the expired pointer value.

The same general rules apply when an allocated object is deallocated or when a
thread-local object's associated thread terminates.

A pointer object's continued existence does not guarantee that the object it
previously designated still exists.

## Storage Reuse and Object Identity

The end of an object's lifetime may allow its former storage to be reused for
another object.

For example, after dynamically allocated storage has been deallocated,
a subsequent allocation may reuse the same memory address.

However, the new allocation provides a new object with its own lifetime.

The fact that two objects occupy the same address at different times does not
make them the same object.

Likewise, a successful `realloc()` operation ends the lifetime of the original
allocated object and provides a new one, even if the returned pointer represents
the same address.

Pointers to the original object cannot be used to access the replacement object
merely because its address is numerically identical.

**Storage reuse does not extend or restore an expired object's lifetime.**

These rules distinguish the identity of a C object from the physical memory
region in which an implementation stores it.

## Special Lifetime Considerations

The existence of an object is necessary, but not sufficient, for every possible
access to that object to have defined behavior.

An access must also satisfy other applicable language requirements, including
those concerning initialization, effective type, alignment, array bounds, and
synchronization between threads.

Some objects also have lifetime rules that differ from the ordinary rules
associated with their storage duration.

In particular, C11 introduced **temporary lifetime** for objects referred to by
certain non-lvalue structure or union expressions containing array members.

Such objects have automatic storage duration, but their lifetimes end when
evaluation of the containing full expression ends.

Attempting to modify an object with temporary lifetime results in undefined
behavior. The standard also permits such an object not to have a unique address.

Temporary lifetime is not a fifth storage duration. It is a
special lifetime rule applying to certain objects with automatic
storage duration.

For additional information, see the earlier subsection
**Compound Literals and Temporary Lifetime** under
**Automatic Storage Duration**.

# CONSTRAINTS

The C language imposes constraints on declarations that determine an object's
storage duration, including restrictions on storage-class specifiers,
variable-length arrays, and initialization.

These constraints govern which declarations are permitted. They are distinct
from the rules governing operations performed during or outside an object's
lifetime.

A violation of a language constraint requires a diagnostic from a conforming
implementation, although an implementation may subsequently accept the program
as an extension.

## Storage-Class Specifiers

At most one storage-class specifier may ordinarily appear in the declaration
specifiers of a declaration.

C23 permits certain combinations, including `thread_local` with either `static`
or `extern`, and `constexpr` with `static`. Additional combinations involving
`auto` and `constexpr` are subject to the language's type-inference and
declaration rules.

Since C11, an object declared with thread storage duration at block scope must
also be declared with `static` or `extern`.

```c
/* C23 */

void example(void)
{
    /* Valid: thread storage duration. */
    static thread_local int counter = 0;

    /* Invalid: thread_local requires static or extern here. */
    /* thread_local int invalid = 0; */
}
```

If an object is declared with a thread-storage specifier, every
declaration of that object must include the thread-storage specifier.

The thread-storage specifier cannot appear in a function declaration.
Functions are not objects and do not have storage duration.

Function parameters always have automatic storage duration. The only
storage-class specifier permitted in a parameter declaration is
`register`, which does not change the parameter's storage duration.

## Variable-Length Arrays

Introduced in C99, variable-length arrays are subject to additional
restrictions concerning storage duration.

An object with static or thread storage duration cannot have a
variable-length array type.

```c
#include <stddef.h>

void example(size_t count)
{
    if (count == 0)
        return;

    /* Valid: VLA with automatic storage duration. */
    int values[count];

    /* Invalid: a VLA cannot have static storage duration. */
    /* static int invalid[count]; */

    (void)values;
}
```

An identifier declared with a variably modified type must have no linkage and
must have block scope or function prototype scope.

This restriction concerns variably modified types generally, while the
prohibition on static and thread storage duration specifically concerns objects
of variable-length array type.

A pointer to a variable-length array is not itself an array object, and is,
therefore, subject to different storage-duration restrictions.

Whenever a variable-length array bound is evaluated, its value *must* be
greater than zero.

The language also prohibits jumping from outside the scope of an identifier
having a variably modified type into that scope.

Note that support for variable-length arrays with automatic storage duration
is a **conditional feature** in C11 and later. An implementation may
omit support for such objects while still supporting other forms
of variably modified types, including pointers to VLA types.

## Initialization Requirements

Objects with static or thread storage duration are subject to restrictions on
their initializers.

All expressions in their initializers must be constant expressions
or string literals, as permitted by the applicable initialization rules.

```c
int compute_value(void);

void example(void)
{
    /* Valid: constant initializer. */
    static int first = 42;

    /* Invalid: function call is not a constant expression. */
    /* static int second = compute_value(); */
}
```

Unlike ordinary automatic objects, objects with static or thread storage
duration cannot generally be initialized using values computed by arbitrary
function calls during program execution.

A block-scope declaration of an identifier with external or internal linkage
must not contain an initializer for that identifier.

Since C23, objects declared with `constexpr` are subject to additional
restrictions on their types and initializers, including requirements for
constant initialization.

## Compound Literals

Since C23, compound literals may include storage-class specifiers,
subject to the applicable declaration constraints.

A compound literal must specify a complete object type or an array
of unknown size, but not a variable-length array type.

The storage-class specifiers must satisfy the constraints that would
apply to the corresponding object definition at the associated scope.

In particular, a compound literal with thread storage duration
associated with block scope must include `static` alongside
`thread_local`.

The C23 compound-literal rules also specify undefined behavior for
the use of unsupported storage-class specifiers.

These restrictions do not change the fundamental lifetime rules
for compound-literal objects.

## External-Linkage Inline Definitions

An inline definition of a function with external linkage must not
contain a definition of a modifiable object with static or thread
storage duration.

It also must not reference an identifier with internal linkage.

This restriction applies specifically to inline definitions of
functions with external linkage. It does not prohibit ordinary
functions or internal-linkage inline functions from containing
block-scope static objects.

# IMPLEMENTATION NOTES

The C standard specifies the lifetime and observable behavior of objects,
but generally leaves their physical storage and management to the
implementation.

Compilers, linkers, operating systems, and runtime libraries employ
different mechanisms to satisfy these requirements. The following
examples illustrate notable implementation techniques and extensions;
they are not additional guarantees of ISO C.

## GCC: Automatic Storage and Stack Reuse

On many architectures, compilers implement automatic objects using
storage associated with a function's stack frame. However, an automatic
object is not required to reside in memory on a hardware stack.

A compiler may instead keep an object's value in registers, optimize
away its physical storage, or reuse storage for other objects whose
lifetimes do not overlap.

GCC exposes one aspect of this behavior through its `-fstack-reuse`
option, which controls reuse of stack space occupied by local objects
and compiler-generated temporaries.

The available settings include:

- `-fstack-reuse=all`: Allow reuse for local variables and temporaries.
- `-fstack-reuse=named_vars`: Limit reuse to named local variables.
- `-fstack-reuse=none`: Disable this stack-reuse optimization.

GCC ordinarily defaults to `-fstack-reuse=all` [1].

This optimization illustrates why the lifetime of an automatic object
must not be confused with the persistence of bytes at a particular
memory address.

Even when a stack slot has not yet been overwritten, accessing an
object after its lifetime has ended remains undefined behavior.
Disabling stack reuse does not extend an object's lifetime.

## GCC: Automatic Variable Initialization Hardening

GCC 12, first released in May 2022, introduced the
`-ftrivial-auto-var-init` option to initialize certain automatic
variables that lack explicit initializers in their source code [2].

The option supports two particularly useful modes:

- `-ftrivial-auto-var-init=zero`: Fill affected variables with zeros.
- `-ftrivial-auto-var-init=pattern`: Fill affected variables with a
  recognizable pattern intended to expose programming errors.

These modes can help mitigate security vulnerabilities associated
with uninitialized storage or make certain defects easier to detect.

However, GCC continues to treat variables without explicit source
initializers as uninitialized for diagnostic and optimization purposes.
Warnings such as `-Wuninitialized` remain applicable [3].

This compiler feature does not establish a general ISO C guarantee
that uninitialized automatic objects contain zero or any other
particular value.

The distinction between an object's lifetime and the validity of
its stored value therefore remains essential, even when an
implementation deliberately initializes otherwise uninitialized
storage as a hardening measure.

## ELF: Static Storage and Executable Sections

On systems using the Executable and Linkable Format (ELF), compilers
and linkers commonly place objects with static storage duration into
different sections according to their initialization and other
properties.

Common sections include:

- `.data`: Writable data with nonzero initial contents.
- `.bss`: Writable data whose initial contents are zero.
- `.rodata`: Read-only data, when suitable for the implementation.

The `.bss` section is particularly notable because it normally
occupies memory at runtime without requiring corresponding zero-filled
bytes to be stored in the executable file.

Instead, the program-loading environment arranges for the required
memory to contain zeros [4].

The placement of an object into one of these sections does not
determine its storage duration. An object can have static storage
duration regardless of the particular section chosen by the
compiler or linker.

Furthermore, compilers may optimize away objects whose physical
storage is not required to preserve observable program behavior.

These conventions are properties of particular toolchains and executable
formats, not requirements imposed by ISO C.

## ELF and GCC: Thread-Local Storage

On many ELF-based systems, thread storage duration is implemented using a
mechanism known as **thread-local storage** (TLS).

ELF provides dedicated sections for representing thread-local data,
including:

- `.tdata`: Thread-local data with an initialization image.
- `.tbss`: Thread-local data requiring zero initialization.

An ELF `PT_TLS` program header describes the initialization template used to
establish thread-local storage for individual threads [5].

The runtime creates the appropriate per-thread instances, while
compiler-generated instructions access the instance associated with the
executing thread.

The exact addressing mechanism depends on the architecture, linkage model,
and runtime environment.

GCC also provides the extension `__thread`, which predates the standardization
of `_Thread_local` in C11 [6].

GCC 3.3, released in May 2003, added support for thread-local storage on its
IA-32 and x86-64 targets [7].

For example:

```c
/* GNU C extension. */

static __thread unsigned int counter = 0;
```

This declaration uses a GCC-specific extension rather than an ISO C
storage-class specifier.

Where supported, GCC can implement standardized thread storage duration using
the same underlying TLS infrastructure.

The existence of ELF TLS machinery does not mean ISO C requires ELF, special
TLS sections, or any particular instruction sequence.

## Microsoft C: Thread-Local Storage

Microsoft's C implementation provides an implementation-specific thread-local
storage facility through the `__declspec(thread)` attribute [8].

For example:

```c
/* Microsoft-specific extension. */

__declspec(thread) int counter = 0;
```

This declaration specifies a distinct instance of `counter` for each thread
through Microsoft's statically allocated TLS mechanism.

Windows also provides a separate runtime TLS interface through functions such as
`TlsAlloc()`, `TlsGetValue()`, and `TlsSetValue()` [9].

The compiler-based mechanism and the runtime TLS interface provide different
ways of associating data with individual threads.

Neither the `__declspec(thread)` syntax nor the Windows TLS API is specified
by ISO C.

These facilities illustrate how a platform may support per-thread state through
mechanisms that differ from those used by ELF-based systems.

## glibc: Allocated Storage and Memory Arenas

The GNU C Library (glibc) provides an implementation of the standard memory
allocation functions that manages storage through internal structures known
as **arenas**.

Its allocator is derived from ptmalloc, which was itself based on Doug Lea's
dlmalloc allocator, first written in 1987 [10, 11].

An arena manages memory obtained from the operating system and subdivides it
into smaller allocation units, commonly called chunks.

glibc can maintain multiple arenas to reduce contention between threads
performing allocations concurrently.

Depending on allocation size, configuration, and runtime behavior, glibc may
obtain storage through mechanisms such as expanding the process heap or
creating anonymous memory mappings with `mmap()`.

Consequently, storage obtained through `malloc()` is not necessarily part of a
single contiguous region of memory.

Similarly, calling `free()` does not necessarily cause the underlying memory
to be immediately returned to the operating system; the allocator may retain it
for subsequent allocations.

These implementation details do not alter the lifetime rules specified by ISO C.

Once an allocation has been deallocated, its allocated object's lifetime has
ended, regardless of whether the allocator retains the corresponding memory
region internally.

The C standard specifies the observable semantics of allocation and
deallocation, not the algorithms or operating-system mechanisms used to
implement them.

# SEE ALSO

[`c-static(7)`](../keywords/static.md), `c-compound-literals(7)`,
`c-initialization(7)`, `c-linkage(7)`, `c-scope(7)`

# STANDARDS

The rules governing object storage duration and lifetime are specified
by ANSI C89 and subsequent C standards.

The original standardized storage-duration model included automatic,
static, and allocated storage duration. Later revisions introduced
additional lifetime rules and storage-duration facilities.

The following revisions are particularly relevant:

- **C89/C90:** Standardized automatic, static, and allocated storage
  duration and their associated object-lifetime rules.
- **C99:** Introduced variable-length arrays and compound literals,
  including their associated storage-duration and lifetime rules.
- **C11:** Introduced thread storage duration through `_Thread_local`,
  temporary lifetime for certain structure and union expressions,
  and the `aligned_alloc()` memory allocation function.
- **C17:** Retained the four storage durations established by C11,
  without introducing a new storage-duration category.
- **C23:** Introduced the `thread_local` keyword, permitted explicit
  storage-class specifiers in compound literals, introduced `constexpr`,
  and added the `free_sized()` and `free_aligned_sized()` functions.

Relevant clauses of ISO/IEC 9899:2024 (C23) include:

- **6.2.4**   -- Storage durations of objects
- **6.2.8**   -- Alignment of objects
- **6.5.1**   -- General (effective type and object access)
- **6.5.3.6** -- Compound literals
- **6.7.2**   -- Storage-class specifiers
- **6.7.5**   -- Function specifiers
- **6.7.7.3** -- Array declarators
- **6.7.11**  -- Initialization
- **7.24.4**  -- Memory management functions
- **7.28.6**  -- Thread-specific storage functions

# HISTORY

## Prior Art

The concepts underlying C's storage durations predate the language itself.
Earlier programming languages and runtime systems distinguished between
storage associated with individual function invocations and storage that
persisted independently of those invocations.

During the 1950s, list-processing languages helped establish dynamic
storage management as an important programming technique. The
Information Processing Language (IPL), developed by Allen Newell,
Cliff Shaw, and Herbert Simon, employed dynamically managed list
structures for symbolic computation [12].

John McCarthy's Lisp, developed from 1958 onward, further advanced
list processing and incorporated automatic storage reclamation through
garbage collection. Unlike C's explicit allocation and deallocation
model, this allowed the runtime to reclaim certain objects that were
no longer accessible to the program [12].

ALGOL 60 introduced influential rules for block-structured
declarations and recursive procedures. It also included the `own`
declaration modifier, which preserved a variable's value across
successive executions of its associated block [13].

Although their semantics were not identical, these earlier facilities
demonstrate that automatic storage, persistent local objects, and
dynamic storage management were established programming concepts
before the development of C.

## Early C

Ken Thompson's **B** language, the immediate predecessor of C, already
used these distinctions. Thompson's _Users' Reference to B_, dated
January 7th, 1972, described three storage classes: automatic,
external, and internal. Automatic storage was allocated for each
function invocation, while external storage existed before program
execution. Internal storage was associated with a function and
persisted across its invocations [14].

Dennis Ritchie's _C Reference Manual_, dated January 15th, 1974,
documented four declarable storage classes: automatic, static,
external, and register. Automatic and register variables had lifetimes
associated with function invocations, while static variables retained
their values between calls. External variables existed independently
of individual functions [15].

These historical storage classes should not be confused with the four
storage durations defined by modern ISO C. In particular, `extern`
and `register` were storage-class specifiers, not distinct storage
durations. The language's terminology and formal separation of storage
duration from linkage developed through subsequent standardization.

Dynamic memory allocation was available through library facilities
during the early development of C and UNIX, before the familiar
ISO C memory-management interfaces were standardized.

The Fifth Edition UNIX Programmer's Manual, published in June 1974,
documents `alloc()` and `free()` in an entry dated March 1st, 1974.
The `alloc()` function accepted a requested size in bytes and returned
a pointer to a suitably aligned region of storage. Its companion
`free()` function made previously allocated storage available for
reuse [16].

The allocator employed a first-fit allocation strategy, coalescing
adjacent free blocks and requesting additional memory from UNIX
through `sbrk()` when necessary. If additional storage could not
be obtained, the documented implementation printed an error message
and terminated execution [16].

By Seventh Edition UNIX in 1979, the library documented the now
familiar `malloc()`, `calloc()`, `realloc()`, and `free()` interfaces.
Its allocator continued to use a circular first-fit strategy and
obtained additional storage through `sbrk()` [17].

The surviving Seventh Edition implementation also illustrates the
relationship between language-level allocation and operating-system
memory management. The allocator maintained its own bookkeeping for
allocated and free blocks, while relying on UNIX to expand the memory
available to the process [18].

Historical allocation behavior was not identical to that required
by modern ISO C. Seventh Edition UNIX permitted certain uses of
`realloc()` on recently freed blocks and warned that a failed
reallocation might destroy the original block [17].

Under modern ISO C, calling `realloc()` with a pointer to previously
deallocated storage has undefined behavior. Furthermore, when an
otherwise valid reallocation request fails, the original allocation
remains intact.

## Standardized C

Looking back on C's development in April 1993, Dennis Ritchie
distinguished the language's two directly expressed storage durations,
automatic and static, from dynamically allocated storage provided
through library routines [19].

The first standardized C language, ANSI X3.159-1989 (C89), and its
international successor ISO/IEC 9899:1990 (C90), formally classified
automatic, static, and allocated storage as three storage durations.
Later revisions extended this model to account for additional kinds
of objects and execution contexts [20].

C99 introduced variable-length arrays and compound literals. These
features extended the lifetime rules for automatic objects and provided
a standardized means of creating unnamed objects through expressions [20].

C11 introduced thread storage duration through `_Thread_local`,
establishing a distinct object instance for each thread. It also
introduced the temporary-lifetime rules for certain structure and
union expressions, as well as `aligned_alloc()` [20, 21].

C23 retained the four storage durations while extending the ways
storage duration can be specified. It introduced `thread_local` as
a keyword, allowed storage-class specifiers in compound literals,
and introduced `constexpr` and new deallocation facilities [20, 22].

## Chronology and Milestones

The following milestones summarize the development of object storage
duration and how it relates to C:

- **1956:** Newell, Shaw, and Simon's IPL work demonstrates
  dynamically managed list structures for symbolic computation [12].
- **1958--1960:** Lisp development establishes influential mechanisms
  for dynamic list allocation and automatic storage reclamation [12].
- **1960 (ALGOL 60):** Formalizes block-structured variable declarations
  and includes the `own` modifier for persistent local values [13].
- **1969--1970:** Ken Thompson develops B from Martin Richards's
  BCPL, establishing the immediate predecessor of C [19].
- **1971--1973:** Dennis Ritchie develops C from B, introducing
  types and other language features [19].
- **January 7th, 1972:** Thompson's B reference manual documents
  automatic, external, and internal storage classes [14].
- **January 15th, 1974:** Ritchie's C Reference Manual documents
  automatic, static, external, and register storage classes [15].
- **March 1st, 1974:** The date of the `alloc(III)` entry preserved
  in the Fifth Edition UNIX Programmer's Manual [16].
- **1978:** Kernighan and Ritchie publish the first edition of
  _The C Programming Language_ [23].
- **1979 (Seventh Edition UNIX):** Documents `malloc()`, `calloc()`,
  `realloc()`, and `free()`, with an allocator based on circular
  first-fit searching and `sbrk()` [17, 18].
- **C89/C90:** Standardized automatic and static storage duration,
  together with dynamic memory allocation through the standard library.
  Technical Corrigendum 2 (1996) explicitly recognized allocated
  storage duration as a third category.
- **1999 (C99):** Introduces variable-length arrays and compound
  literals, extending the rules for object lifetime [20].
- **May 14th, 2003 (GCC 3.3):** Adds thread-local storage support for
  IA-32 and x86-64, illustrating implementation support for per-thread
  objects before their standardization in C11 [7].
- **2011 (C11):** Introduces thread storage duration, temporary
  lifetime, and aligned allocation [20, 21].
- **2018 (C17):** Retains the four-duration model without introducing
  another storage-duration category [20].
- **2024 (C23):** Introduces `thread_local`, storage-class specifiers
  in compound literals, `constexpr`, and sized deallocation [20, 22].

# REFERENCES

1. GNU Project, _Using the GNU Compiler Collection_,
   _Code Generation Options_, `-fstack-reuse`.
   https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html

2. GNU Project, _GCC 12 Release Series: Changes, New Features,
   and Fixes_, 2022.
   https://gcc.gnu.org/gcc-12/changes.html

3. GNU Project, _Using the GNU Compiler Collection_,
   _Optimize Options_, `-ftrivial-auto-var-init`.
   https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html

4. GNU Project, _Using as_, `.bss` section.
   https://sourceware.org/binutils/docs/as/bss.html

5. Oracle, _Linker and Libraries Guide_,
   _Thread-Local Storage_, ELF `.tdata`, `.tbss`, and `PT_TLS`.
   https://docs.oracle.com/cd/E19683-01/816-7777/chapter7-13/index.html

6. GNU Project, _Using the GNU Compiler Collection_,
   _Thread-Local_, the `__thread` extension.
   https://gcc.gnu.org/onlinedocs/gcc/Thread-Local.html

7. GNU Project, _GCC 3.3 Release Series: Changes, New Features,
   and Fixes_, IA-32/x86-64 TLS support, 2003.
   https://gcc.gnu.org/gcc-3.3/changes.html

8. Microsoft, _Thread Local Storage_, Microsoft C language reference.
   https://learn.microsoft.com/en-us/cpp/c-language/thread-local-storage?view=msvc-170

9. Microsoft, _Using Thread Local Storage_, Windows API documentation.
   https://learn.microsoft.com/en-us/windows/win32/procthread/using-thread-local-storage

10. GNU Project, _The GNU C Library Reference Manual_,
    GNU allocator, arenas, and memory mappings.
    https://sourceware.org/glibc/manual/latest/html_mono/libc.html

11. Doug Lea, _A Memory Allocator_, historical description of
    the development of dlmalloc, beginning in 1987.
    https://www.cs.princeton.edu/courses/archive/fall19/cos217/asgts/06heapmgr/LeaMalloc.html

12. John McCarthy, _History of Lisp_, ACM SIGPLAN Notices,
    volume 13, number 8, August 1978.
    https://pages.cs.wisc.edu/~horwitz/CS704-NOTES/PAPERS/mccarthy.pdf

13. Peter Naur et al., _Revised Report on the Algorithmic Language
    ALGOL 60_, 1963, particularly section 5, _Declarations_.
    https://www.masswerk.at/algol60/report.htm

14. Ken Thompson, _Users' Reference to B_, Bell Laboratories,
    January 7, 1972.
    https://cm-bell-labs.github.io/who/dmr/kbman.html

15. Dennis M. Ritchie, _C Reference Manual_, Bell Laboratories
    Technical Memorandum 74-1273-1, January 15, 1974.
    https://cm-bell-labs.github.io/who/dmr/cman74.pdf

16. Bell Laboratories, _UNIX Programmer's Manual_, Fifth Edition,
    `alloc(III)`, entry dated March 1, 1974; manual published June 1974.
    https://ftp.icm.edu.pl/packages/UnixArchive/Distributions/Research/Dennis_v5/v5man.pdf

17. Bell Laboratories, _UNIX Programmer's Manual_, Seventh Edition,
    `malloc(3)`, January 1979.
    https://www.retro11.de/ouxr/u7ed/usr/man/man3/malloc.3
    Alternative archive: https://www.unix.com/man-page/v7/3/malloc/

18. Bell Laboratories, Seventh Edition UNIX source,
    `usr/src/libc/gen/malloc.c`, January 1979.
    https://www.retro11.de/ouxr/u7ed/usr/src/libc/gen/malloc.c.html

19. Dennis M. Ritchie, _The Development of the C Language_,
    Second History of Programming Languages Conference, 1993.
    https://cm-bell-labs.github.io/who/dmr/chist.html

20. ISO/IEC JTC1/SC22/WG14, _Project Status and Milestones_,
    history of published C language standards.
    https://www.open-std.org/jtc1/sc22/wg14/www/projects.html

21. ISO/IEC JTC1/SC22/WG14, _Programming Languages -- C_,
    Committee Draft N1570, April 2011 (C11).
    https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf

22. ISO/IEC, _Information technology -- Programming languages -- C_,
    ISO/IEC 9899:2024 (C23).
    https://www.iso.org/standard/82075.html

    Closely corresponding public working draft N3220,
    February 22, 2024 (early C2y draft).
    https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf

23. Brian W. Kernighan and Dennis M. Ritchie,
    _The C Programming Language_, first edition,
    Prentice-Hall, 1978.
