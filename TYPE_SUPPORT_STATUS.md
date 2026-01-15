# Type Support Status in AXIS

## Current Status Summary

### ✅ **i32 (32-bit signed integer)** - FULLY SUPPORTED
- ✅ Boolean NOT operator (`!`) - Works on `bool` type
- ✅ Arithmetic negation (`-`) - **WORKS**
- ✅ Bool type with `True`/`False` - **WORKS**
- ✅ Comments (`//` and `#`) - **WORKS**
- ✅ All arithmetic operators (+, -, *, /, %)
- ✅ All bitwise operators (&, |, ^, <<, >>)
- ✅ All comparison operators (==, !=, <, >, <=, >=)

### ⚠️ **Other Integer Types** - PARTIALLY SUPPORTED

#### Semantic Analyzer: ✅ COMPLETE
- ✅ Type checking for i8, i16, i32, i64, u8, u16, u32, u64
- ✅ Literal coercion (i32 literals can be assigned to any integer type)
- ✅ Type safety enforcement
- ✅ Proper error messages

#### Code Generator: ❌ LIMITED
- ❌ Only generates 32-bit x86-64 instructions (eax, dword)
- ❌ No support for 8-bit registers (al, ah)
- ❌ No support for 16-bit registers (ax)
- ❌ No support for 64-bit operations (rax, qword)
- ❌ All types currently treated as i32 at code generation

---

## What Works Today

### ✅ All Features on i32
```axis
func main() -> i32:
    # Boolean NOT
    flag: bool = True
    when !flag:
        give 1
    
    # Arithmetic negation
    x: i32 = 10
    y: i32 = -x
    when y == -10:
        give 42
    
    # Comments
    // C-style comment
    # Python-style comment
    
    give 0
```

**Result:** ✅ Compiles and runs correctly

---

## What Needs Implementation

### Code Generator Enhancements Needed

#### 1. **Register Selection Based on Type**
- i8 → `al` (8-bit)
- i16 → `ax` (16-bit)  
- i32 → `eax` (32-bit) ✅ Current
- i64 → `rax` (64-bit)

#### 2. **Instruction Prefixes**
- i8: Use `movzx` for load, direct `mov` for store
- i16: Use operand-size override prefix (0x66)
- i32: Current behavior ✅
- i64: Use REX.W prefix (0x48)

#### 3. **Memory Operations**
- i8: `byte ptr [rbp-X]`
- i16: `word ptr [rbp-X]`
- i32: `dword ptr [rbp-X]` ✅ Current
- i64: `qword ptr [rbp-X]`

#### 4. **Stack Allocation**
Currently allocates 4 bytes per variable.
Should allocate:
- i8/u8: 1 byte (align to 1)
- i16/u16: 2 bytes (align to 2)
- i32/u32: 4 bytes (align to 4)
- i64/u64: 8 bytes (align to 8)

---

## Implementation Roadmap

### Phase 1: i32 Features (COMPLETE ✅)
- [x] Boolean type and NOT operator
- [x] Arithmetic negation
- [x] Comments (both styles)
- [x] Literal coercion in semantic analyzer

### Phase 2: Code Generator Architecture (FUTURE)
- [ ] Refactor register allocation
- [ ] Add type-aware instruction selection
- [ ] Implement proper stack layout
- [ ] Add assembler support for 8/16/64-bit operations

### Phase 3: Full Type Support (FUTURE)
- [ ] i8/u8 code generation
- [ ] i16/u16 code generation
- [ ] i64/u64 code generation
- [ ] Comprehensive type tests

---

## Testing Status

### ✅ Tests That Pass (i32)
- `test_simple_negation_i32.axis` ✅
- All bool tests ✅
- All comment tests ✅
- All stress tests (using i32) ✅

### ❌ Tests That Fail (Other Types)
- `test_negation_all_types.axis` - Code generator limitation
- `test_negation_edge_cases_all_types.axis` - Code generator limitation  
- `test_negation_arithmetic_all_types.axis` - Code generator limitation

**Reason:** Code generator only emits 32-bit instructions regardless of declared type.

---

## Workaround for Users

**Current Recommendation:**
Use `i32` for all integer operations until multi-type support is added to the code generator.

```axis
# ✅ WORKS
func main() -> i32:
    x: i32 = 100
    y: i32 = -x
    give y

# ❌ DOESN'T WORK YET (compiles but treats as i32)
func main() -> i32:
    x: i8 = 100
    y: i8 = -x
    give y  # Would need to return i32, not i8
```

---

## Conclusion

**All new features (Boolean NOT, arithmetic negation, bool type, comments) work perfectly on i32.**

Support for i8, i16, and i64 requires code generator enhancements which are planned for a future release. The semantic analyzer is already fully prepared for all types - only the backend code generation needs to be implemented.

---

**Status Date:** January 15, 2026  
**Version:** AXIS 1.0.1-beta  
**Tested:** All features on i32 ✅
