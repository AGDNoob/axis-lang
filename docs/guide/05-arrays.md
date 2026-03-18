# Arrays

## Declaring Arrays

Arrays have a fixed size and element type:

```axis
numbers: (i32; 5) = [1, 2, 3, 4, 5]
empty: (i32; 3)                        # zero-initialized: [0, 0, 0]
```

The syntax is `(element_type; size)`.

## Accessing Elements

```axis
arr: (i32; 3) = [10, 20, 30]
writeln(arr[0])    # 10
writeln(arr[2])    # 30

arr[1] = 99
writeln(arr[1])    # 99
```

Indexing is zero-based.

> **Note:** AXIS does not perform runtime bounds checking. Accessing an index outside the array size is undefined behavior.

## Supported Element Types

Arrays can hold any integer type or `str`:

```axis
bytes: (u8; 4) = [0, 127, 200, 255]
words: (i64; 2) = [1000000, 2000000]
names: (str; 3) = ["Alice", "Bob", "Charlie"]
```

## Compound Assignment on Elements

```axis
arr: (i32; 3) = [10, 20, 30]
arr[0] += 5      # 15
arr[1] *= 2      # 40
arr[2] -= 10     # 20
```

## Iterating Over Arrays

```axis
arr: (i32; 4) = [1, 2, 3, 4]
for x in arr:
    writeln(x)
```

## Copying Arrays

To copy an array, you must use the `copy` keyword. This prevents accidental aliasing:

```axis
original: (i32; 3) = [1, 2, 3]
duplicate: (i32; 3) = copy original

duplicate[0] = 99
writeln(original[0])    # still 1
```

Without `copy`, the compiler will give an error. This is intentional — AXIS wants you to be explicit about when data is duplicated.

### Copy Modes

In compile mode, there are two copy variants:

```axis
arr2: (i32; 5) = copy arr1            # default (same as copy.runtime)
arr3: (i32; 5) = copy.runtime arr1    # explicit runtime-optimized
arr4: (i32; 5) = copy.compile arr1    # compile-time-optimized
```

- **`copy` / `copy.runtime`** — Uses `REP MOVSB` for memory-to-memory copy. Fast at runtime, especially for larger arrays.
- **`copy.compile`** — Generates an inline byte-copy loop. Produces a smaller binary but may be slightly slower for large copies.

In script mode, both behave identically.

## Next

[Fields](06-fields.md)
