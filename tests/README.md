# AXIS Test Programs

This directory contains example programs and test cases for the AXIS compiler.

---

## 📝 Example Programs

### Basic Tests

**test_return42.axis** - Simplest possible program
```rust
fn main() -> i32 {
    return 42;
}
```
Expected: Exit code 42

**test_arithmetic.axis** - Basic arithmetic
```rust
fn main() -> i32 {
    let x: i32 = 10;
    let y: i32 = 20;
    return x + y;  // 30
}
```
Expected: Exit code 30

**test_variables.axis** - Variable declarations
```rust
fn main() -> i32 {
    let x: i32 = 5;
    let mut y: i32 = 10;
    y = y + x;
    return y;  // 15
}
```
Expected: Exit code 15

### Control Flow

**test_if_else.axis** - Conditional branching
```rust
fn main() -> i32 {
    let x: i32 = 10;
    if x > 5 {
        return 1;
    } else {
        return 0;
    }
}
```
Expected: Exit code 1

**test_while_loop.axis** - Loop iteration
```rust
fn main() -> i32 {
    let mut i: i32 = 0;
    while i < 10 {
        i = i + 1;
    }
    return i;  // 10
}
```
Expected: Exit code 10

**test_break.axis** - Break statement
```rust
fn main() -> i32 {
    let mut i: i32 = 0;
    while i < 100 {
        if i == 42 {
            break;
        }
        i = i + 1;
    }
    return i;  // 42
}
```
Expected: Exit code 42

**test_continue.axis** - Continue statement
```rust
fn main() -> i32 {
    let mut sum: i32 = 0;
    let mut i: i32 = 0;
    
    while i < 10 {
        i = i + 1;
        if i < 5 {
            continue;
        }
        sum = sum + i;
    }
    
    return sum;  // 5+6+7+8+9+10 = 45
}
```
Expected: Exit code 45

### Complex Examples

**test_complex.axis** - Combined features
```rust
fn main() -> i32 {
    let mut result: i32 = 0;
    let mut i: i32 = 0;
    
    while i < 5 {
        if i > 2 {
            result = result + i;
        }
        i = i + 1;
    }
    
    return result;  // 3 + 4 = 7
}
```
Expected: Exit code 7

**syntax_test.axis** - Full language showcase
```rust
fn main() -> i32 {
    // Variables
    let x: i32 = 10;
    let mut counter: i32 = 0;
    
    // Conditionals
    if x >= 5 {
        counter = counter + 1;
    }
    
    // Loops
    let mut i: i32 = 0;
    while i < 3 {
        if i == 1 {
            i = i + 1;
            continue;
        }
        counter = counter + 1;
        i = i + 1;
    }
    
    return counter;
}
```

---

## 🧪 Running Tests

### Compile and Run Individual Test

```bash
# Using installer (Linux)
axis build test_return42.axis -o test_return42
./test_return42
echo $?

# Using compiler directly
python3 ../compilation_pipeline.py test_return42.axis -o test_return42 --elf
chmod +x test_return42
./test_return42
echo $?
```

### Quick Test (Compile + Check Exit Code)

```bash
axis run test_arithmetic.axis
```

### Run All Tests (Bash Script)

```bash
#!/bin/bash
for file in *.axis; do
    echo "Testing $file..."
    axis build "$file" -o "${file%.axis}" --elf
    ./"${file%.axis}"
    echo "Exit code: $?"
    echo ""
done
```

---

## ✅ Expected Outputs

| Test File             | Exit Code | Description              |
|-----------------------|-----------|--------------------------|
| test_return42.axis    | 42        | Simple return            |
| test_arithmetic.axis  | 30        | 10 + 20                  |
| test_variables.axis   | 15        | Variable mutation        |
| test_if_else.axis     | 1         | Conditional branch       |
| test_while_loop.axis  | 10        | Basic loop               |
| test_break.axis       | 42        | Break from loop          |
| test_continue.axis    | 45        | Skip iterations          |
| test_complex.axis     | 7         | Combined features        |

---

## 🚀 Creating Your Own Tests

### Basic Template

```rust
fn main() -> i32 {
    // Your code here
    return 0;
}
```

### Debugging Tips

1. **Check compilation output**
   ```bash
   axis build yourtest.axis -o yourtest --elf -v
   ```

2. **Examine generated assembly** (verbose mode shows instructions)

3. **Use exit codes for verification** (0-255 range on Linux)

4. **Hexdump binary for debugging**
   ```bash
   hexdump -C yourtest | head -20
   ```

---

## 📦 Test Organization

- **Simple tests** - Single feature per file
- **Complex tests** - Multiple features combined
- **Syntax tests** - Language showcase
- **Edge cases** - Boundary conditions

---

## 🔧 Adding New Tests

1. Create `test_feature.axis` in this directory
2. Write minimal code to test specific feature
3. Document expected exit code
4. Run test to verify
5. Add to this README

---

**Note:** Binary executables (compiled tests) are ignored by git. Only `.axis` source files are tracked.
