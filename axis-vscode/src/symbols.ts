export type SymbolDoc = {
    name: string;
    kind: 'function' | 'type' | 'keyword';
    signature: string;
    summary: string;
    example?: string;
    snippet?: string;
};

export const AXIS_SYMBOLS: SymbolDoc[] = [
    {
        name: 'write',
        kind: 'function',
        signature: 'func write(value: any) -> void',
        summary: 'Write a value to standard output without a trailing newline.',
        example: 'write("Hello, ")\nwrite("World")',
        snippet: 'write(${0})',
    },
    {
        name: 'writeln',
        kind: 'function',
        signature: 'func writeln(value: any) -> void',
        summary: 'Write a value to standard output followed by a newline.',
        example: 'writeln("Hello, World!")',
        snippet: 'writeln(${0})',
    },
    {
        name: 'input',
        kind: 'function',
        signature: 'func input(prompt: str) -> str',
        summary:
            'Read one line from standard input after printing `prompt`. The ' +
            'result is a `str` which can be cast with `as` to numeric types. ' +
            'When a cast fails, the paired `<var>_input_failed()` helper ' +
            'returns `True` on the next call.',
        example:
            'age: i32 = input("Age: ") as i32\n' +
            'when age_input_failed():\n' +
            '    writeln("Please enter a valid integer.")',
        snippet: 'input(${1:"prompt: "})',
    },
    {
        name: 'range',
        kind: 'function',
        signature: 'func range(start: i64, end: i64) -> range',
        summary:
            'Half-open integer range [start, end) for use with `for ... in`. ' +
            'Values yielded match the declared loop variable type.',
        example: 'for i in range(0, 10):\n    writeln(i)',
        snippet: 'range(${1:0}, ${2:10})',
    },
    {
        name: 'syscall',
        kind: 'function',
        signature: 'syscall(num: i64, ...args) -> i64',
        summary:
            'Invoke a raw OS system call. Number and argument conventions ' +
            'match the host kernel ABI (Linux: rax/rdi/rsi/rdx/r10/r8/r9).',
        example: 'syscall(1, 1, buf, len)  // write(stdout, buf, len)',
        snippet: 'syscall(${1:num}, ${0})',
    },
    {
        name: 'i8',
        kind: 'type',
        signature: 'type i8',
        summary: 'Signed 8-bit integer. Range: -128 .. 127.',
    },
    {
        name: 'i16',
        kind: 'type',
        signature: 'type i16',
        summary: 'Signed 16-bit integer. Range: -32 768 .. 32 767.',
    },
    {
        name: 'i32',
        kind: 'type',
        signature: 'type i32',
        summary:
            'Signed 32-bit integer. Default integer type in most examples.',
    },
    {
        name: 'i64',
        kind: 'type',
        signature: 'type i64',
        summary: 'Signed 64-bit integer.',
    },
    {
        name: 'u8',
        kind: 'type',
        signature: 'type u8',
        summary: 'Unsigned 8-bit integer. Range: 0 .. 255.',
    },
    {
        name: 'u16',
        kind: 'type',
        signature: 'type u16',
        summary: 'Unsigned 16-bit integer. Range: 0 .. 65 535.',
    },
    {
        name: 'u32',
        kind: 'type',
        signature: 'type u32',
        summary: 'Unsigned 32-bit integer.',
    },
    {
        name: 'u64',
        kind: 'type',
        signature: 'type u64',
        summary: 'Unsigned 64-bit integer.',
    },
    {
        name: 'bool',
        kind: 'type',
        signature: 'type bool',
        summary: 'Boolean type. Values: `True`, `False`.',
    },
    {
        name: 'str',
        kind: 'type',
        signature: 'type str',
        summary: 'UTF-8 string. Immutable and length-prefixed.',
    },
    {
        name: 'ptr',
        kind: 'type',
        signature: 'type ptr',
        summary: 'Opaque pointer. Used for syscall arguments and FFI.',
    },
    {
        name: 'void',
        kind: 'type',
        signature: 'type void',
        summary: 'The empty return type. Use on functions that return nothing.',
    },
];

export const KEYWORD_DOCS: Record<string, string> = {
    mode: 'Program mode declaration. Must appear at the top of the file.\n\n' +
        '- `mode script` — run directly, cached native binary on first run.\n' +
        '- `mode compile` — produce a standalone executable.',
    func: 'Declare a function.\n\n```axis\nfunc name(param: type) -> ret_type:\n    body\n```',
    return: 'Return a value from the enclosing function.',
    when: 'Conditional block. Equivalent to `if` in C-family languages. ' +
        'Use `else:` or `else when <cond>:` for additional branches.',
    else: 'Else branch of a `when` chain.',
    while: 'Loop while a condition is true.',
    loop: 'Infinite loop. Exit with `stop` / `break`.',
    repeat: 'Repeat block. Typically paired with an exit condition.',
    for: 'Iteration loop. Usually combined with `in range(a, b)`.',
    in: 'Iterator-source clause for `for`.',
    match: 'Pattern match over a value. Each arm is `<pattern>:` followed ' +
        'by an indented block. Use `_` as a wildcard arm.',
    break: 'Exit the innermost loop.',
    stop: 'Alias for `break`. With `@label`, exits the labelled loop.',
    continue: 'Skip to the next iteration of the innermost loop.',
    skip: 'Alias for `continue`. With `@label`, skips the labelled loop.',
    field: 'Declare a struct-like aggregate type.\n\n```axis\nPoint: field:\n    x: i32\n    y: i32\n```',
    enum: 'Declare an enum type (optionally with an underlying integer type).',
    const: 'Bind an immutable value that cannot be reassigned.',
    update: 'Parameter modifier: pass by mutable reference.',
    copy: 'Parameter modifier: pass by value (explicit copy).',
    as: 'Type cast expression. `x as i64` converts `x` to an `i64`.',
    and: 'Short-circuiting logical AND.',
    or: 'Short-circuiting logical OR.',
    not: 'Logical NOT.',
    syscall: 'Raw system call. Kernel-ABI specific.',
    True: 'Boolean literal `True`.',
    False: 'Boolean literal `False`.',
    script: 'Script execution mode. See `mode`.',
    compile: 'Compile-to-binary mode. See `mode`.',
};
