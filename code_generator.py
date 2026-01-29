"""
AXIS Code Generator - x86-64 backend
typisierten AST -> assembly -> assembler backend macht den rest
"""

import re
from typing import List, Optional, Set
from syntactic_analyzer import *
from semantic_analyzer import SemanticAnalyzer


class RegisterAllocator:
    """
    Simple register allocator for x86-64.
    
    Uses callee-saved registers (rbx, r12-r15) for temporaries.
    These must be saved/restored in function prologue/epilogue.
    Falls back to stack when all registers are exhausted.
    """
    
    # For now, don't use callee-saved registers to avoid stack offset conflicts
    # All temporaries will spill to stack via push/pop
    TEMP_REGS_64 = []
    TEMP_REGS_32 = []
    TEMP_REGS_16 = []
    TEMP_REGS_8 = []
    
    def __init__(self):
        self.free_regs: List[str] = list(self.TEMP_REGS_64)  # Stack of free registers
        self.used_regs: Set[str] = set()  # Registers currently in use
        self.saved_regs: Set[str] = set()  # Registers that need save/restore
        self.spill_count = 0  # Track stack spills for debugging
    
    def reset(self):
        """Reset allocator state for a new function."""
        self.free_regs = list(self.TEMP_REGS_64)
        self.used_regs = set()
        self.saved_regs = set()
        self.spill_count = 0
    
    def allocate(self) -> tuple[str, bool]:
        """
        Allocate a register.
        Returns (register_name, is_spill).
        If is_spill is True, the "register" is actually a stack push.
        """
        if self.free_regs:
            reg = self.free_regs.pop()
            self.used_regs.add(reg)
            self.saved_regs.add(reg)  # Mark for save/restore in prologue
            return (reg, False)
        else:
            # All registers in use - need to spill to stack
            self.spill_count += 1
            return ('_spill_', True)
    
    def release(self, reg: str):
        """Release a register back to the free pool."""
        if reg == '_spill_':
            return  # Stack spill, nothing to release
        if reg in self.used_regs:
            self.used_regs.remove(reg)
            self.free_regs.append(reg)
    
    def get_reg_for_type(self, base_reg: str, type_: str) -> str:
        """Get the appropriate register size for a type."""
        try:
            idx = self.TEMP_REGS_64.index(base_reg)
        except ValueError:
            return base_reg  # Not a temp reg, return as-is
        
        if type_ in ['i8', 'u8']:
            return self.TEMP_REGS_8[idx]
        elif type_ in ['i16', 'u16']:
            return self.TEMP_REGS_16[idx]
        elif type_ in ['i32', 'u32', 'bool']:
            return self.TEMP_REGS_32[idx]
        else:  # i64, u64, str
            return self.TEMP_REGS_64[idx]
    
    def get_save_restore_regs(self) -> List[str]:
        """Get list of registers that need to be saved in prologue."""
        return sorted(list(self.saved_regs))


class CodeGenerator:
    """
    x86-64 code generator für AXIS
    
    calling convention: System V AMD64 (linux halt)
    - args: rdi, rsi, rdx, rcx, r8, r9 (für i32: edi, esi, etc)
    - return: rax/eax
    - callee-saved: rbx, rbp, r12-r15
    
    expression evaluation:
    - ergebnis landet in eax/rax
    - temp register: ecx, edx
    
    stack layout (klassisch):
    - [rbp+0]  = saved rbp
    - [rbp-4]  = erste local var
    - [rbp-8]  = zweite, usw
    """
    
    def __init__(self):
        self.asm_lines: List[str] = []
        self.label_counter = 0
        self.current_function = None
        self.loop_stack = []  # Stack für break/continue: (continue_label, break_label)
        
        # Register allocator for temporaries
        self.reg_alloc = RegisterAllocator()
        
        # String literals für .rodata section
        self.string_data: dict[str, tuple[str, int]] = {}  # label -> (content, length)
        self.string_counter = 0
        
        # Spezial strings für bool output
        self.true_label = None
        self.false_label = None
        
        # Read error flag - global runtime state
        self.needs_read_failed_flag = False
        
        # System V AMD64 Argument-Register
        self.arg_regs_64 = ['rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9']
        self.arg_regs_32 = ['edi', 'esi', 'edx', 'ecx', 'r8d', 'r9d']
    
    def emit(self, line: str):
        self.asm_lines.append(line)
    
    def emit_label(self, label: str):
        self.asm_lines.append(f"{label}:")
    
    def fresh_label(self, prefix: str = "L") -> str:
        label = f"{prefix}_{self.label_counter}"
        self.label_counter += 1
        return label
    
    def get_register(self, base_reg: str, type_: str) -> str:
        """Get the appropriate register name for a given type."""
        if type_ == 'i8' or type_ == 'u8':
            # 8-bit registers: al, bl, cl, dl
            reg_map = {'a': 'al', 'b': 'bl', 'c': 'cl', 'd': 'dl'}
            return reg_map.get(base_reg, base_reg + 'l')
        elif type_ == 'i16' or type_ == 'u16':
            return base_reg + 'x' if base_reg in 'abcd' else base_reg
        elif type_ == 'i64' or type_ == 'u64':
            return 'r' + base_reg + 'x' if base_reg in 'abcd' else base_reg
        else:  # i32, u32, bool, default
            return 'e' + base_reg + 'x' if base_reg in 'abcd' else base_reg
    
    def get_register(self, base_reg: str, type_: str) -> str:
        """Get the appropriate register name for a given type."""
        if type_ == 'i8' or type_ == 'u8':
            # 8-bit registers: al, bl, cl, dl
            reg_map = {'a': 'al', 'b': 'bl', 'c': 'cl', 'd': 'dl'}
            return reg_map.get(base_reg, base_reg + 'l')
        elif type_ == 'i16' or type_ == 'u16':
            return base_reg + 'x' if base_reg in 'abcd' else base_reg
        elif type_ == 'i64' or type_ == 'u64':
            return 'r' + base_reg + 'x' if base_reg in 'abcd' else base_reg
        else:  # i32, u32, bool, default
            return 'e' + base_reg + 'x' if base_reg in 'abcd' else base_reg
    
    def get_mov_size(self, type_: str) -> str:
        """Get the memory size specifier for mov instructions."""
        if type_ == 'i8' or type_ == 'u8':
            return 'byte'
        elif type_ == 'i16' or type_ == 'u16':
            return 'word'
        elif type_ == 'i64' or type_ == 'u64':
            return 'qword'
        else:  # i32, u32, bool, default
            return 'dword'
    
    def compile(self, program: Program) -> str:
        self.asm_lines = []
        self.label_counter = 0
        self.string_data = {}
        self.string_counter = 0
        self.true_label = None
        self.false_label = None
        self.needs_read_failed_flag = False
        
        # Store enum definitions for code generation
        # Enums are compile-time constants, no code generated here
        self.enum_defs = {e.name: e for e in program.enum_defs} if program.enum_defs else {}
        
        # Generiere Code für alle Funktionen
        for func in program.functions:
            self.compile_function(func)
        
        return '\n'.join(self.asm_lines)
    
    def get_string_data(self) -> dict[str, tuple[str, int]]:
        """Returns collected string data for .rodata section"""
        return self.string_data
    
    def needs_bss_section(self) -> bool:
        """Returns True if code needs BSS section (for _read_failed flag)"""
        return self.needs_read_failed_flag
    
    def needs_bss_section(self) -> bool:
        """Returns True if we need a .bss section (for read_failed flag)"""
        return self.needs_read_failed_flag
    
    def add_string(self, content: str) -> str:
        """Add string to data section, return its label"""
        # Check if string already exists
        for label, (existing_content, _) in self.string_data.items():
            if existing_content == content:
                return label
        
        label = f"_str_{self.string_counter}"
        self.string_counter += 1
        self.string_data[label] = (content, len(content))
        return label
    
    def get_bool_labels(self) -> tuple[str, str]:
        """Get or create True/False string labels"""
        if self.true_label is None:
            self.true_label = self.add_string("True")
        if self.false_label is None:
            self.false_label = self.add_string("False")
        return self.true_label, self.false_label
    
    def compile_function(self, func: Function):
        self.current_function = func
        
        # Reset register allocator for this function
        self.reg_alloc.reset()
        
        # Track update parameters for copy-out on return
        self.update_params = []  # list of (param, ptr_slot_offset, value_slot_offset, type)
        
        # Compile body first
        body_asm = self.asm_lines
        self.asm_lines = []
        self.compile_block(func.body)
        body_code = self.asm_lines
        self.asm_lines = body_asm
        
        # Function Label
        self.emit_label(func.name)
        
        # Standard prologue
        self.emit("push rbp")
        self.emit("mov rbp, rsp")
        
        # Stack-Space allokieren (already 16-byte aligned by semantic analyzer)
        if func.stack_size > 0:
            self.emit(f"sub rsp, {func.stack_size}")
        
        # Copy parameters from registers/stack to local stack slots
        # System V AMD64: rdi, rsi, rdx, rcx, r8, r9
        # Args 7+ are at [rbp+16], [rbp+24], etc. (after saved rbp and return addr)
        param_regs_64 = ['rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9']
        param_regs_32 = ['edi', 'esi', 'edx', 'ecx', 'r8d', 'r9d']
        param_regs_16 = ['di', 'si', 'dx', 'cx', 'r8w', 'r9w']
        param_regs_8 = ['dil', 'sil', 'dl', 'cl', 'r8b', 'r9b']
        
        for i, param in enumerate(func.params):
            offset = param.stack_offset
            param_type = param.type
            is_update = param.modifier == 'update'
            
            # Get source register or stack location
            if i < 6:
                src_reg_64 = param_regs_64[i]
                src_reg_32 = param_regs_32[i]
                src_reg_16 = param_regs_16[i]
                src_reg_8 = param_regs_8[i]
                from_stack = False
            else:
                # Args 7+ come from caller's stack
                caller_offset = 16 + (i - 6) * 8
                from_stack = True
            
            if is_update:
                # For update params: register/stack contains a pointer to caller's variable
                # 1. Save the pointer in ptr_slot
                # 2. Dereference to get value into local copy (copy-in)
                ptr_slot = param.ptr_slot_offset
                
                if from_stack:
                    # Load pointer from caller's stack, store in ptr_slot
                    self.emit(f"mov r11, qword [rbp+{caller_offset}]")
                    self.emit(f"mov qword [rbp{ptr_slot:+d}], r11")
                else:
                    # Store pointer from register to ptr_slot
                    self.emit(f"mov qword [rbp{ptr_slot:+d}], {src_reg_64}")
                    self.emit(f"mov r11, {src_reg_64}")
                
                # Now dereference pointer (in r11) to copy value into local slot
                if param_type in ['i64', 'u64', 'str']:
                    self.emit(f"mov rax, qword [r11]")
                    self.emit(f"mov qword [rbp{offset:+d}], rax")
                elif param_type in ['i8', 'u8']:
                    self.emit(f"mov al, byte [r11]")
                    self.emit(f"mov byte [rbp{offset:+d}], al")
                elif param_type in ['i16', 'u16']:
                    self.emit(f"mov ax, word [r11]")
                    self.emit(f"mov word [rbp{offset:+d}], ax")
                else:  # i32, u32, bool
                    self.emit(f"mov eax, dword [r11]")
                    self.emit(f"mov dword [rbp{offset:+d}], eax")
                
                # Track for copy-out in epilogue
                self.update_params.append((param, ptr_slot, offset, param_type))
            else:
                # Regular or copy params: just copy value to local slot
                if from_stack:
                    if param_type in ['i64', 'u64', 'str']:
                        self.emit(f"mov rax, qword [rbp+{caller_offset}]")
                        self.emit(f"mov qword [rbp{offset:+d}], rax")
                    elif param_type in ['i8', 'u8']:
                        self.emit(f"mov al, byte [rbp+{caller_offset}]")
                        self.emit(f"mov byte [rbp{offset:+d}], al")
                    elif param_type in ['i16', 'u16']:
                        self.emit(f"mov ax, word [rbp+{caller_offset}]")
                        self.emit(f"mov word [rbp{offset:+d}], ax")
                    else:  # i32, u32, bool
                        self.emit(f"mov eax, dword [rbp+{caller_offset}]")
                        self.emit(f"mov dword [rbp{offset:+d}], eax")
                else:
                    if param_type in ['i64', 'u64', 'str']:
                        self.emit(f"mov qword [rbp{offset:+d}], {src_reg_64}")
                    elif param_type in ['i8', 'u8']:
                        self.emit(f"mov byte [rbp{offset:+d}], {src_reg_8}")
                    elif param_type in ['i16', 'u16']:
                        self.emit(f"mov word [rbp{offset:+d}], {src_reg_16}")
                    else:  # i32, u32, bool
                        self.emit(f"mov dword [rbp{offset:+d}], {src_reg_32}")
        
        # Add body code
        self.asm_lines.extend(body_code)
        
        # Epilog (falls kein explizites Return)
        self.emit_label(f"{func.name}_epilog")
        
        # Copy-out for update parameters
        for param, ptr_slot, value_slot, param_type in self.update_params:
            self.emit(f"; copy-out {param.name}")
            self.emit(f"mov r10, qword [rbp{ptr_slot:+d}]")  # load pointer
            if param_type in ['i64', 'u64', 'str']:
                self.emit(f"mov rax, qword [rbp{value_slot:+d}]")
                self.emit(f"mov qword [r10], rax")
            elif param_type in ['i8', 'u8']:
                self.emit(f"mov al, byte [rbp{value_slot:+d}]")
                self.emit(f"mov byte [r10], al")
            elif param_type in ['i16', 'u16']:
                self.emit(f"mov ax, word [rbp{value_slot:+d}]")
                self.emit(f"mov word [r10], ax")
            else:  # i32, u32, bool
                self.emit(f"mov eax, dword [rbp{value_slot:+d}]")
                self.emit(f"mov dword [r10], eax")
        
        # Standard epilogue
        self.emit("mov rsp, rbp")
        self.emit("pop rbp")
        self.emit("ret")
        
        self.emit("")  # Leerzeile zwischen Funktionen
        self.current_function = None
    
    def compile_block(self, block: Block):
        for stmt in block.statements:
            self.compile_statement(stmt)
    
    def compile_statement(self, stmt: Statement):
        # dispatch zu verschiedenen statement types
        if isinstance(stmt, VarDecl):
            self.compile_vardecl(stmt)
        elif isinstance(stmt, Assignment):
            self.compile_assignment(stmt)
        elif isinstance(stmt, CompoundAssignment):
            self.compile_compound_assignment(stmt)
        elif isinstance(stmt, Return):
            self.compile_return(stmt)
        elif isinstance(stmt, If):
            self.compile_if(stmt)
        elif isinstance(stmt, While):
            self.compile_while(stmt)
        elif isinstance(stmt, ForLoop):
            self.compile_for_loop(stmt)
        elif isinstance(stmt, Break):
            self.compile_break(stmt)
        elif isinstance(stmt, Continue):
            self.compile_continue(stmt)
        elif isinstance(stmt, Write):
            self.compile_write(stmt)
        elif isinstance(stmt, ExprStatement):
            self.compile_expression(stmt.expression)
        elif isinstance(stmt, Match):
            self.compile_match(stmt)
        elif isinstance(stmt, EnumDef):
            # Enums are compile-time constants, no code generation needed
            pass
        else:
            raise NotImplementedError(f"Statement type not implemented: {type(stmt).__name__}")
    
    def compile_vardecl(self, vardecl: VarDecl):
        if vardecl.init:
            # Init-Expression evaluieren -> eax/rax
            self.compile_expression(vardecl.init)
            
            # Store zu Stack-Slot - type aware
            # vardecl.stack_offset ist negativ (z.B. -4)
            var_type = vardecl.type
            if var_type in ['i8', 'u8']:
                self.emit(f"mov byte [rbp{vardecl.stack_offset:+d}], al")
            elif var_type in ['i16', 'u16']:
                self.emit(f"mov word [rbp{vardecl.stack_offset:+d}], ax")
            elif var_type in ['i64', 'u64', 'str']:
                # str is a pointer, need 64-bit
                self.emit(f"mov qword [rbp{vardecl.stack_offset:+d}], rax")
            else:
                self.emit(f"mov [rbp{vardecl.stack_offset:+d}], eax")
    
    def compile_assignment(self, assign: Assignment):
        # Pointer dereference removed in v1.1.0 - AXIS uses value semantics
        
        # Value evaluieren -> eax
        self.compile_expression(assign.value)
        
        # Target muss Identifier sein
        if not isinstance(assign.target, Identifier):
            raise NotImplementedError("Assignment target must be identifier")
        
        # Store zu Stack-Slot - type aware
        symbol = assign.target.symbol
        if symbol.type in ['i8', 'u8']:
            self.emit(f"mov byte [rbp{symbol.stack_offset:+d}], al")
        elif symbol.type in ['i16', 'u16']:
            self.emit(f"mov word [rbp{symbol.stack_offset:+d}], ax")
        elif symbol.type in ['i64', 'u64', 'str']:
            # str is a pointer, need 64-bit
            self.emit(f"mov qword [rbp{symbol.stack_offset:+d}], rax")
        else:
            self.emit(f"mov [rbp{symbol.stack_offset:+d}], eax")
    
    def compile_compound_assignment(self, stmt: CompoundAssignment):
        """
        Compile compound assignment: x += 1, x -= 2, etc.
        Equivalent to: x = x op value
        """
        # Target muss Identifier sein
        if not isinstance(stmt.target, Identifier):
            raise NotImplementedError("Compound assignment target must be identifier")
        
        symbol = stmt.target.symbol
        
        # Load current value into eax/rax
        if symbol.type in ['i8', 'u8']:
            self.emit(f"movzx eax, byte [rbp{symbol.stack_offset:+d}]")
        elif symbol.type in ['i16', 'u16']:
            self.emit(f"movzx eax, word [rbp{symbol.stack_offset:+d}]")
        elif symbol.type in ['i64', 'u64', 'str']:
            self.emit(f"mov rax, qword [rbp{symbol.stack_offset:+d}]")
        else:
            self.emit(f"mov eax, [rbp{symbol.stack_offset:+d}]")
        
        # Push current value to save it
        self.emit("push rax")
        
        # Evaluate the right-hand side value
        self.compile_expression(stmt.value)
        
        # Pop current value into rcx
        self.emit("mov rcx, rax")  # rcx = rhs value
        self.emit("pop rax")       # rax = current value
        
        # Perform the operation: rax = rax op rcx
        if stmt.operator == '+':
            self.emit("add rax, rcx")
        elif stmt.operator == '-':
            self.emit("sub rax, rcx")
        elif stmt.operator == '*':
            self.emit("imul rax, rcx")
        elif stmt.operator == '/':
            # Signed division: rax = rax / rcx
            self.emit("cqo")           # Sign-extend rax into rdx:rax
            self.emit("idiv rcx")      # rax = rdx:rax / rcx
        elif stmt.operator == '%':
            # Modulo: rdx = rax % rcx
            self.emit("cqo")
            self.emit("idiv rcx")
            self.emit("mov rax, rdx")  # Result is in rdx
        elif stmt.operator == '&':
            self.emit("and rax, rcx")
        elif stmt.operator == '|':
            self.emit("or rax, rcx")
        elif stmt.operator == '^':
            self.emit("xor rax, rcx")
        elif stmt.operator == '<<':
            self.emit("mov cl, cl")    # Shift amount in cl
            self.emit("shl rax, cl")
        elif stmt.operator == '>>':
            self.emit("mov cl, cl")    # Shift amount in cl  
            self.emit("sar rax, cl")   # Arithmetic shift right
        else:
            raise NotImplementedError(f"Compound assignment operator not implemented: {stmt.operator}")
        
        # Store result back to variable - type aware
        if symbol.type in ['i8', 'u8']:
            self.emit(f"mov byte [rbp{symbol.stack_offset:+d}], al")
        elif symbol.type in ['i16', 'u16']:
            self.emit(f"mov word [rbp{symbol.stack_offset:+d}], ax")
        elif symbol.type in ['i64', 'u64', 'str']:
            self.emit(f"mov qword [rbp{symbol.stack_offset:+d}], rax")
        else:
            self.emit(f"mov [rbp{symbol.stack_offset:+d}], eax")

    # compile_deref_assignment removed in v1.1.0 - AXIS uses value semantics
    
    def compile_return(self, ret: Return):
        if ret.value:
            # Value evaluieren -> eax
            self.compile_expression(ret.value)
        
        # Jump zu Epilog
        self.emit(f"jmp {self.current_function.name}_epilog")
    
    def compile_if(self, if_stmt: If):
        else_label = self.fresh_label("if_else")
        end_label = self.fresh_label("if_end")
        
        # Condition evaluieren -> eax (bool: 0 oder 1)
        self.compile_expression(if_stmt.condition)
        
        # Test ob false (0)
        self.emit("cmp eax, 0")
        
        if if_stmt.else_block:
            self.emit(f"je {else_label}")
        else:
            self.emit(f"je {end_label}")
        
        # Then-Block
        self.compile_block(if_stmt.then_block)
        
        if if_stmt.else_block:
            self.emit(f"jmp {end_label}")
            self.emit_label(else_label)
            self.compile_block(if_stmt.else_block)
        
        self.emit_label(end_label)
    
    def compile_while(self, while_stmt: While):
        cond_label = self.fresh_label("while_cond")
        body_label = self.fresh_label("while_body")
        end_label = self.fresh_label("while_end")
        
        # Loop-Stack für break/continue
        self.loop_stack.append((cond_label, end_label))
        
        # Condition
        self.emit_label(cond_label)
        self.compile_expression(while_stmt.condition)
        self.emit("cmp eax, 0")
        self.emit(f"je {end_label}")
        
        # Body
        self.emit_label(body_label)
        self.compile_block(while_stmt.body)
        self.emit(f"jmp {cond_label}")
        
        # End
        self.emit_label(end_label)
        
        self.loop_stack.pop()
    
    def compile_break(self, break_stmt: Break):
        if not self.loop_stack:
            raise RuntimeError("Break outside of loop")
        
        _, break_label = self.loop_stack[-1]
        self.emit(f"jmp {break_label}")
    
    def compile_continue(self, continue_stmt: Continue):
        # continue jump - geht zu loop start
        if not self.loop_stack:
            raise RuntimeError("Continue outside of loop")
        
        continue_label, _ = self.loop_stack[-1]
        self.emit(f"jmp {continue_label}")
    
    def compile_for_loop(self, for_stmt: ForLoop):
        """
        Compile for loop.
        
        For range iteration (for i in range(start, end, step)):
            Initialize i = start
            Loop:
                if i >= end (or i <= end for negative step): exit
                body
                i = i + step
        
        For array iteration (for x in arr):
            Initialize index = 0
            Loop:
                if index >= array_size: exit
                x = arr[index]
                body
                index++
        """
        loop_label = self.fresh_label("for_loop")
        end_label = self.fresh_label("for_end")
        continue_label = self.fresh_label("for_continue")
        
        var_symbol = for_stmt.var_symbol
        var_type = for_stmt.var_type
        
        if isinstance(for_stmt.iterable, RangeExpr):
            # Range iteration
            range_expr = for_stmt.iterable
            
            # Initialize loop variable with start value
            self.compile_expression(range_expr.start)
            self._store_to_stack(var_symbol.stack_offset, var_type)
            
            # Determine step direction
            has_step = range_expr.step is not None
            
            # Loop start
            self.emit_label(loop_label)
            
            # Load current value
            self._load_from_stack(var_symbol.stack_offset, var_type)
            self.emit("push rax")  # Save current value
            
            # Load end value and compare
            self.compile_expression(range_expr.end)
            self.emit("mov rbx, rax")  # end in rbx
            self.emit("pop rax")       # current in rax
            
            # Compare: if i >= end (for positive step) or i <= end (for negative step), exit
            # For simplicity, always use i < end for ascending (default)
            self.emit("cmp rax, rbx")
            self.emit(f"jge {end_label}")  # exit if i >= end
            
            # Loop stack for break/continue
            self.loop_stack.append((continue_label, end_label))
            
            # Body
            self.compile_block(for_stmt.body)
            
            # Continue label (increment)
            self.emit_label(continue_label)
            
            # Increment: i = i + step (default step is 1)
            self._load_from_stack(var_symbol.stack_offset, var_type)
            if has_step:
                self.emit("push rax")
                self.compile_expression(range_expr.step)
                self.emit("mov rbx, rax")
                self.emit("pop rax")
                self.emit("add rax, rbx")
            else:
                self.emit("inc rax")
            self._store_to_stack(var_symbol.stack_offset, var_type)
            
            self.emit(f"jmp {loop_label}")
            
            self.emit_label(end_label)
            self.loop_stack.pop()
        
        else:
            # Array iteration - not yet implemented in compile mode
            raise NotImplementedError("Array iteration in for loops not yet supported in compile mode")
    
    def _load_from_stack(self, offset: int, var_type: str):
        """Load a value from stack into rax/eax"""
        if var_type == 'i8':
            self.emit(f"movsx eax, byte [rbp{offset:+d}]")
        elif var_type == 'u8':
            self.emit(f"movzx eax, byte [rbp{offset:+d}]")
        elif var_type == 'i16':
            self.emit(f"movsx eax, word [rbp{offset:+d}]")
        elif var_type == 'u16':
            self.emit(f"movzx eax, word [rbp{offset:+d}]")
        elif var_type in ['i64', 'u64', 'str']:
            self.emit(f"mov rax, qword [rbp{offset:+d}]")
        else:
            self.emit(f"mov eax, [rbp{offset:+d}]")
    
    def _store_to_stack(self, offset: int, var_type: str):
        """Store value from rax/eax to stack"""
        if var_type in ['i8', 'u8']:
            self.emit(f"mov byte [rbp{offset:+d}], al")
        elif var_type in ['i16', 'u16']:
            self.emit(f"mov word [rbp{offset:+d}], ax")
        elif var_type in ['i64', 'u64', 'str']:
            self.emit(f"mov qword [rbp{offset:+d}], rax")
        else:
            self.emit(f"mov [rbp{offset:+d}], eax")
    
    def compile_match(self, match_stmt: Match):
        """
        Compile match statement as a chain of compare-and-jump.
        
        match value:
            1:
                do_something()
            2:
                do_other()
            _:
                default()
        
        Becomes:
            eval value -> rax
            cmp rax, 1
            je .arm_0
            cmp rax, 2
            je .arm_1
            jmp .arm_wildcard (or .end if no wildcard)
        .arm_0:
            do_something()
            jmp .end
        .arm_1:
            do_other()
            jmp .end
        .arm_wildcard:
            default()
        .end:
        """
        end_label = self.fresh_label("match_end")
        
        # Evaluate the match value into rax
        self.compile_expression(match_stmt.value)
        self.emit("push rax")  # Save value for comparisons
        
        # Determine the type size for comparison
        value_type = getattr(match_stmt, 'value_type', 'i32')
        
        # Generate labels for each arm
        arm_labels = []
        wildcard_label = None
        wildcard_arm = None
        
        for i, arm in enumerate(match_stmt.arms):
            if arm.is_wildcard:
                wildcard_label = self.fresh_label("match_wildcard")
                wildcard_arm = arm
            else:
                arm_labels.append((arm, self.fresh_label(f"match_arm_{i}")))
        
        # Generate comparison chain
        for arm, label in arm_labels:
            self.emit("mov rax, [rsp]")  # Reload value
            self.compile_expression(arm.pattern)  # Pattern value in rax
            self.emit("mov rbx, rax")  # Pattern in rbx
            self.emit("mov rax, [rsp]")  # Value in rax
            self.emit("cmp rax, rbx")
            self.emit(f"je {label}")
        
        # Jump to wildcard or end if no match
        if wildcard_label:
            self.emit(f"jmp {wildcard_label}")
        else:
            self.emit(f"jmp {end_label}")
        
        # Generate arm bodies
        for arm, label in arm_labels:
            self.emit_label(label)
            self.compile_block(arm.body)
            self.emit(f"jmp {end_label}")
        
        # Generate wildcard body if present
        if wildcard_arm:
            self.emit_label(wildcard_label)
            self.compile_block(wildcard_arm.body)
        
        self.emit_label(end_label)
        self.emit("add rsp, 8")  # Clean up saved value
    
    def compile_enum_access(self, enum_access: EnumAccess):
        """
        Compile enum variant access: Color.Red -> load immediate value
        The variant_value is set by the semantic analyzer.
        """
        value = enum_access.variant_value
        self.emit(f"mov eax, {value}")
    
    def compile_write(self, write_stmt: Write):
        """
        write() / writeln() - output to stdout via syscall
        
        Handling:
        - str: ptr in rax, length known from string data
        - integers: convert to decimal string, then print
        - bool: print "True" or "False"
        - enums: print underlying integer value
        """
        value_type = write_stmt.value_type
        
        if value_type == 'str':
            self.compile_write_string(write_stmt)
        elif value_type == 'bool':
            self.compile_write_bool(write_stmt)
        elif value_type in ('i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64'):
            self.compile_write_integer(write_stmt)
        elif value_type in self.enum_defs:
            # Enums are printed as their underlying integer value
            self.compile_write_integer(write_stmt)
        else:
            raise NotImplementedError(f"Cannot write type: {value_type}")
    
    def compile_write_string(self, write_stmt: Write):
        """Write string to stdout"""
        # Expression muss StringLiteral sein oder Identifier mit str type
        value = write_stmt.value
        
        if isinstance(value, StringLiteral):
            # String literal - add to data section
            label = self.add_string(value.value)
            length = len(value.value)
            
            # write(1, ptr, len) syscall
            # Use movabs with placeholder - pipeline patches actual address
            self.emit(f"; write string literal")
            self.emit(f"mov rax, 1")                    # syscall: write
            self.emit(f"mov rdi, 1")                    # fd: stdout
            self.emit(f"movabs rsi, @{label}")          # ptr to string (placeholder)
            self.emit(f"mov rdx, {length}")             # length
            self.emit("syscall")
        elif isinstance(value, Identifier):
            # String variable - null-terminated, need to calculate length
            symbol = value.symbol
            self.emit(f"; write string variable (null-terminated)")
            self.emit(f"mov rsi, qword [rbp{symbol.stack_offset:+d}]")  # load string ptr
            
            # Calculate length (find null terminator) - using scasb-like approach
            self.emit("push rsi")                       # save ptr for write
            strlen_loop = self.fresh_label("strlen")
            strlen_done = self.fresh_label("strlen_done")
            
            self.emit("xor rcx, rcx")                   # length counter
            self.emit("mov r10, rsi")                   # r10 = current ptr
            self.emit_label(strlen_loop)
            self.emit("movzx eax, byte [r10]")          # load byte
            self.emit("test al, al")                    # check for null
            self.emit(f"jz {strlen_done}")
            self.emit("inc rcx")
            self.emit("inc r10")
            self.emit(f"jmp {strlen_loop}")
            self.emit_label(strlen_done)
            
            # rcx = length, rsi was saved on stack
            self.emit("mov rdx, rcx")                   # length
            self.emit("pop rsi")                        # restore string ptr
            self.emit("mov rax, 1")                     # syscall: write
            self.emit("mov rdi, 1")                     # fd: stdout
            self.emit("syscall")
        elif isinstance(value, Read) or isinstance(value, Readln):
            # Direct write of read result - compile the read, ptr in rax
            self.compile_expression(value)
            
            # rax = string pointer, need strlen
            self.emit("; write result of read()")
            self.emit("mov rsi, rax")                   # ptr in rsi
            self.emit("push rsi")
            
            strlen_loop = self.fresh_label("strlen")
            strlen_done = self.fresh_label("strlen_done")
            
            self.emit("xor rcx, rcx")
            self.emit("mov r10, rsi")
            self.emit_label(strlen_loop)
            self.emit("movzx eax, byte [r10]")
            self.emit("test al, al")
            self.emit(f"jz {strlen_done}")
            self.emit("inc rcx")
            self.emit("inc r10")
            self.emit(f"jmp {strlen_loop}")
            self.emit_label(strlen_done)
            
            self.emit("mov rdx, rcx")
            self.emit("pop rsi")
            self.emit("mov rax, 1")
            self.emit("mov rdi, 1")
            self.emit("syscall")
        else:
            raise NotImplementedError(f"Cannot write string expression type: {type(value).__name__}")
        
        # Newline falls writeln
        if write_stmt.newline:
            self.emit_newline()
    
    def compile_write_bool(self, write_stmt: Write):
        """Write bool as True/False"""
        true_label, false_label = self.get_bool_labels()
        
        # Evaluate bool expression
        self.compile_expression(write_stmt.value)
        
        # Check if true or false
        done_label = self.fresh_label("write_bool_done")
        
        self.emit("; write bool")
        self.emit("test al, al")
        self.emit(f"jz .write_false_{self.label_counter}")
        
        # True case
        self.emit(f"mov rax, 1")
        self.emit(f"mov rdi, 1")
        self.emit(f"movabs rsi, @{true_label}")
        self.emit(f"mov rdx, 4")  # len("True")
        self.emit("syscall")
        self.emit(f"jmp {done_label}")
        
        # False case
        self.emit_label(f".write_false_{self.label_counter}")
        self.emit(f"mov rax, 1")
        self.emit(f"mov rdi, 1")
        self.emit(f"movabs rsi, @{false_label}")
        self.emit(f"mov rdx, 5")  # len("False")
        self.emit("syscall")
        
        self.emit_label(done_label)
        
        if write_stmt.newline:
            self.emit_newline()
    
    def compile_write_integer(self, write_stmt: Write):
        """
        Write integer as decimal string
        
        Strategy: Stack-basierte Konversion
        - Push digits auf Stack (reverse order)
        - Pop und write
        """
        value_type = write_stmt.value_type
        is_signed = value_type in ('i8', 'i16', 'i32', 'i64')
        
        # Evaluate expression -> result in rax
        self.compile_expression(write_stmt.value)
        
        # Sign extend if needed for smaller signed types
        if value_type == 'i8':
            self.emit("movsx rax, al")
        elif value_type == 'i16':
            self.emit("movsx rax, ax")
        elif value_type == 'i32':
            self.emit("movsxd rax, eax")
        elif value_type in ('u8', 'u16', 'u32'):
            # Zero extend - upper bits should already be 0 from compile_expression
            pass
        
        # Label für conversion
        skip_neg = self.fresh_label("skip_neg")
        convert_loop = self.fresh_label("convert_loop")
        write_loop = self.fresh_label("write_loop")
        done = self.fresh_label("int_done")
        
        self.emit("; write integer")
        self.emit("push rbx")        # callee-saved
        self.emit("push r12")        # callee-saved  
        self.emit("push r13")        # callee-saved
        
        self.emit("mov r12, rax")    # save original value
        self.emit("xor r13, r13")    # digit counter
        self.emit("xor rbx, rbx")    # negative flag
        
        if is_signed:
            # Check für negative
            self.emit("test rax, rax")
            self.emit(f"jns {skip_neg}")
            self.emit("mov rbx, 1")      # set negative flag
            self.emit("neg rax")         # make positive
            self.emit_label(skip_neg)
        
        # Convert loop: divide by 10, push remainder
        self.emit("mov r12, rax")
        self.emit_label(convert_loop)
        self.emit("xor rdx, rdx")        # clear for div
        self.emit("mov rcx, 10")
        self.emit("div rcx")             # rax = quotient, rdx = remainder
        self.emit("add dl, '0'")         # convert to ASCII
        self.emit("push rdx")            # save digit
        self.emit("inc r13")             # digit count++
        self.emit("test rax, rax")
        self.emit(f"jnz {convert_loop}")
        
        if is_signed:
            # Print minus sign if negative
            skip_minus = self.fresh_label("skip_minus")
            self.emit("test rbx, rbx")
            self.emit(f"jz {skip_minus}")
            # write minus sign
            self.emit("push '-'")        # push '-' char
            self.emit("mov rax, 1")
            self.emit("mov rdi, 1")
            self.emit("mov rsi, rsp")
            self.emit("mov rdx, 1")
            self.emit("syscall")
            self.emit("add rsp, 8")      # pop '-'
            self.emit_label(skip_minus)
        
        # Write digits
        self.emit_label(write_loop)
        self.emit("mov rax, 1")
        self.emit("mov rdi, 1")
        self.emit("mov rsi, rsp")        # digit auf stack
        self.emit("mov rdx, 1")
        self.emit("syscall")
        self.emit("add rsp, 8")          # pop digit
        self.emit("dec r13")
        self.emit(f"jnz {write_loop}")
        
        self.emit("pop r13")
        self.emit("pop r12")
        self.emit("pop rbx")
        
        if write_stmt.newline:
            self.emit_newline()
    
    def emit_newline(self):
        """Emit newline character to stdout"""
        # Add newline string if not exists
        nl_label = self.add_string("\n")
        self.emit("; newline")
        self.emit("mov rax, 1")
        self.emit("mov rdi, 1")
        self.emit(f"movabs rsi, @{nl_label}")
        self.emit("mov rdx, 1")
        self.emit("syscall")
    
    def compile_string_literal(self, string_lit: StringLiteral):
        """Compile string literal - load pointer to rax"""
        label = self.add_string(string_lit.value)
        self.emit(f"movabs rax, @{label}")

    def compile_expression(self, expr: Expression):
        # expression compilation - result landet immer in eax/rax
        if isinstance(expr, Literal):
            self.compile_literal(expr)
        elif isinstance(expr, Identifier):
            self.compile_identifier(expr)
        elif isinstance(expr, BinaryOp):
            self.compile_binaryop(expr)
        elif isinstance(expr, UnaryOp):
            self.compile_unaryop(expr)
        elif isinstance(expr, Call):
            self.compile_call(expr)
        elif isinstance(expr, StringLiteral):
            self.compile_string_literal(expr)
        elif isinstance(expr, Read):
            self.compile_read(expr)
        elif isinstance(expr, Readln):
            self.compile_readln(expr)
        elif isinstance(expr, Readchar):
            self.compile_readchar(expr)
        elif isinstance(expr, ReadFailed):
            self.compile_read_failed(expr)
        elif isinstance(expr, EnumAccess):
            self.compile_enum_access(expr)
        # Pointer expression types removed in v1.1.0 - AXIS uses value semantics
        else:
            raise NotImplementedError(f"Expression type not implemented: {type(expr).__name__}")
    
    def compile_literal(self, lit: Literal):
        # Parse Immediate
        value = self.parse_literal_value(lit.value)
        # Get the inferred type from semantic analysis
        lit_type = getattr(lit, 'inferred_type', 'i32')
        
        # For signed i8, sign-extend negative values properly for comparison
        # When we compare an i8 variable (loaded with movsx) against a literal,
        # both should be in sign-extended i32 form
        if lit_type == 'i8':
            # Sign-extend i8 to i32
            if value < 0:
                value = value  # Already correct as Python int
            elif value > 127:
                value = value - 256  # Convert unsigned 8-bit to signed
        elif lit_type == 'u8':
            value = value & 0xFF  # Zero-extend for unsigned
        elif lit_type in ['i64', 'u64']:
            # Use 64-bit register for i64/u64
            self.emit(f"mov rax, {value}")
            return
        
        self.emit(f"mov eax, {value}")
    
    def parse_literal_value(self, value_str: str) -> int:
        # parse verschiedene number formats - hex, binary, decimal
        value_str = value_str.strip()
        
        if value_str.startswith('0x') or value_str.startswith('0X'):
            return int(value_str, 16)
        elif value_str.startswith('0b') or value_str.startswith('0B'):
            return int(value_str, 2)
        else:
            return int(value_str, 10)
    
    def compile_identifier(self, ident: Identifier):
        # load variable value vom stack in eax
        symbol = ident.symbol
        
        # Parameters are now stored on stack like locals, same access pattern
        # Use type-aware load
        if symbol.type == 'i8':
            # Sign-extend i8 to i32
            self.emit(f"movsx eax, byte [rbp{symbol.stack_offset:+d}]")
        elif symbol.type == 'u8':
            # Zero-extend u8 to i32
            self.emit(f"movzx eax, byte [rbp{symbol.stack_offset:+d}]")
        elif symbol.type == 'i16':
            # Sign-extend i16 to i32
            self.emit(f"movsx eax, word [rbp{symbol.stack_offset:+d}]")
        elif symbol.type == 'u16':
            # Zero-extend u16 to i32
            self.emit(f"movzx eax, word [rbp{symbol.stack_offset:+d}]")
        elif symbol.type in ['i64', 'u64', 'str']:
            # Full 64-bit load (str is a pointer)
            self.emit(f"mov rax, qword [rbp{symbol.stack_offset:+d}]")
        else:
            # Default i32 load
            self.emit(f"mov eax, [rbp{symbol.stack_offset:+d}]")
    
    def compile_binaryop(self, binop: BinaryOp):
        """
        Binary operations with register allocation.
        Uses callee-saved registers for temporaries instead of stack pushes.
        """
        if binop.op in ['+', '-', '*', '/', '%', '&', '|', '^']:
            # Evaluate left side -> eax
            self.compile_expression(binop.left)
            
            # Allocate temp register for left result
            temp_reg, is_spill = self.reg_alloc.allocate()
            
            if is_spill:
                # Fallback to stack if no registers available
                self.emit("push rax")
            else:
                # Save left result in temp register
                self.emit(f"mov {temp_reg}, rax")
            
            # Evaluate right side -> eax
            self.compile_expression(binop.right)
            self.emit("mov ecx, eax")
            
            # Restore left to eax
            if is_spill:
                self.emit("pop rax")
            else:
                self.emit(f"mov rax, {temp_reg}")
                self.reg_alloc.release(temp_reg)
            
            if binop.op == '+':
                self.emit("add eax, ecx")
            elif binop.op == '-':
                self.emit("sub eax, ecx")
            elif binop.op == '*':
                # Multiplikation: eax = eax * ecx
                self.emit("imul ecx")
            elif binop.op == '/':
                # Division: eax = eax / ecx
                # cdq erweitert eax zu edx:eax (sign extend)
                self.emit("cdq")
                self.emit("idiv ecx")
            elif binop.op == '%':
                # Modulo: remainder nach Division in edx
                self.emit("cdq")
                self.emit("idiv ecx")
                self.emit("mov eax, edx")  # remainder von edx nach eax
            elif binop.op == '&':
                # Bitwise AND
                self.emit("and eax, ecx")
            elif binop.op == '|':
                # Bitwise OR
                self.emit("or eax, ecx")
            elif binop.op == '^':
                # Bitwise XOR
                self.emit("xor eax, ecx")
        
        # Shift operators
        elif binop.op in ['<<', '>>']:
            # Evaluate left side -> eax
            self.compile_expression(binop.left)
            
            # Allocate temp register for left result
            temp_reg, is_spill = self.reg_alloc.allocate()
            
            if is_spill:
                self.emit("push rax")
            else:
                self.emit(f"mov {temp_reg}, rax")
            
            # Evaluate right side -> ecx (shift count)
            self.compile_expression(binop.right)
            self.emit("mov ecx, eax")
            
            # Restore left to eax
            if is_spill:
                self.emit("pop rax")
            else:
                self.emit(f"mov rax, {temp_reg}")
                self.reg_alloc.release(temp_reg)
            
            if binop.op == '<<':
                # Left shift: shl eax, cl
                self.emit("shl eax, cl")
            elif binop.op == '>>':
                # Right shift: use sar for signed, shr for unsigned
                left_type = getattr(binop.left, 'inferred_type', 'i32')
                if left_type in ['i8', 'i16', 'i32', 'i64']:
                    # Arithmetic right shift (preserves sign bit)
                    self.emit("sar eax, cl")
                else:
                    # Logical right shift (fills with zeros)
                    self.emit("shr eax, cl")
        
        # comparison operators - result is bool
        elif binop.op in ['==', '!=', '<', '<=', '>', '>=']:
            # Evaluate left side -> eax
            self.compile_expression(binop.left)
            
            # Allocate temp register for left result
            temp_reg, is_spill = self.reg_alloc.allocate()
            
            if is_spill:
                self.emit("push rax")
            else:
                self.emit(f"mov {temp_reg}, rax")
            
            # Evaluate right side -> eax
            self.compile_expression(binop.right)
            self.emit("mov ecx, eax")
            
            # Restore left to eax
            if is_spill:
                self.emit("pop rax")
            else:
                self.emit(f"mov rax, {temp_reg}")
                self.reg_alloc.release(temp_reg)
            
            # Compare
            self.emit("cmp eax, ecx")
            
            # Materialisiere bool ohne setcc (MVP)
            true_label = self.fresh_label("cmp_true")
            end_label = self.fresh_label("cmp_end")
            
            # Jump-Condition basierend auf Operator
            jump_map = {
                '==': 'je',
                '!=': 'jne',
                '<': 'jl',
                '<=': 'jle',
                '>': 'jg',
                '>=': 'jge',
            }
            
            self.emit(f"{jump_map[binop.op]} {true_label}")
            self.emit("mov eax, 0")
            self.emit(f"jmp {end_label}")
            self.emit_label(true_label)
            self.emit("mov eax, 1")
            self.emit_label(end_label)
        
        # Logical AND with short-circuit evaluation
        elif binop.op == 'and':
            false_label = self.fresh_label("and_false")
            end_label = self.fresh_label("and_end")
            
            # Evaluate left side
            self.compile_expression(binop.left)
            self.emit("test eax, eax")
            self.emit(f"jz {false_label}")  # if left is false, skip right
            
            # Evaluate right side (only if left was true)
            self.compile_expression(binop.right)
            self.emit("test eax, eax")
            self.emit(f"jz {false_label}")  # if right is false, result is false
            
            # Both true
            self.emit("mov eax, 1")
            self.emit(f"jmp {end_label}")
            
            self.emit_label(false_label)
            self.emit("mov eax, 0")
            self.emit_label(end_label)
        
        # Logical OR with short-circuit evaluation
        elif binop.op == 'or':
            true_label = self.fresh_label("or_true")
            end_label = self.fresh_label("or_end")
            
            # Evaluate left side
            self.compile_expression(binop.left)
            self.emit("test eax, eax")
            self.emit(f"jnz {true_label}")  # if left is true, skip right
            
            # Evaluate right side (only if left was false)
            self.compile_expression(binop.right)
            self.emit("test eax, eax")
            self.emit(f"jnz {true_label}")  # if right is true, result is true
            
            # Both false
            self.emit("mov eax, 0")
            self.emit(f"jmp {end_label}")
            
            self.emit_label(true_label)
            self.emit("mov eax, 1")
            self.emit_label(end_label)
        
        else:
            raise NotImplementedError(f"Binary operator '{binop.op}' not implemented")
    
    def compile_unaryop(self, unaryop: UnaryOp):
        if unaryop.op == '-':
            # Negation - type aware
            self.compile_expression(unaryop.operand)
            # Get the type of the operand
            op_type = getattr(unaryop.operand, 'inferred_type', 'i32')
            if op_type in ['i64', 'u64']:
                self.emit("neg rax")
            else:
                self.emit("neg eax")
        elif unaryop.op == '!' or unaryop.op == 'not':
            # Boolean NOT: flip 0 to 1 and 1 to 0
            self.compile_expression(unaryop.operand)
            self.emit("xor eax, 1")  # Toggle lowest bit
        else:
            raise NotImplementedError(f"Unary operator '{unaryop.op}' not implemented")
    
    # Pointer compilation functions removed in v1.1.0 - AXIS uses value semantics
    # compile_deref, compile_addressof, compile_sizeof, compile_null_literal all removed
    
    def compile_call(self, call: Call):
        """
        Compile function call with System V AMD64 calling convention.
        - First 6 args: rdi, rsi, rdx, rcx, r8, r9
        - Args 7+: pushed on stack in reverse order
        - 'update' parameters: pass address, callee does copy-in/copy-out
        """
        # Get function info for update parameter handling
        func_symbol = None
        if hasattr(call, 'function_symbol'):
            func_symbol = call.function_symbol
        
        num_args = len(call.args)
        stack_args = max(0, num_args - 6)
        
        # Need to align stack to 16 bytes before call if we push odd number of args
        # Current stack is 16-byte aligned. Each push is 8 bytes.
        need_alignment_padding = (stack_args % 2) == 1
        
        if need_alignment_padding:
            self.emit("sub rsp, 8  ; alignment padding")
        
        # Push stack arguments (args 7+) in reverse order
        for i in range(num_args - 1, 5, -1):
            is_update = func_symbol and i < len(func_symbol.params) and func_symbol.params[i].modifier == 'update'
            if is_update and isinstance(call.args[i], Identifier):
                # Pass address for update params
                symbol = call.args[i].symbol
                self.emit(f"lea rax, [rbp{symbol.stack_offset:+d}]")
            else:
                self.compile_expression(call.args[i])
            self.emit("push rax")
        
        # Evaluate first 6 args and push to temp stack to avoid clobbering
        reg_arg_count = min(num_args, 6)
        for i in range(reg_arg_count):
            is_update = func_symbol and i < len(func_symbol.params) and func_symbol.params[i].modifier == 'update'
            if is_update and isinstance(call.args[i], Identifier):
                # Pass address for update params
                symbol = call.args[i].symbol
                self.emit(f"lea rax, [rbp{symbol.stack_offset:+d}]")
            else:
                self.compile_expression(call.args[i])
            self.emit("push rax")
        
        # Pop first 6 args in reverse order into argument registers
        for i in range(reg_arg_count - 1, -1, -1):
            self.emit("pop rax")
            is_update = func_symbol and i < len(func_symbol.params) and func_symbol.params[i].modifier == 'update'
            
            if is_update:
                # Update params pass a pointer (always 64-bit)
                dest_reg = self.arg_regs_64[i]
                if dest_reg != 'rax':
                    self.emit(f"mov {dest_reg}, rax")
            else:
                # Determine correct register size based on arg type
                arg_type = getattr(call.args[i], 'inferred_type', 'i32')
                if arg_type in ['i64', 'u64', 'str']:
                    dest_reg = self.arg_regs_64[i]
                    if dest_reg != 'rax':
                        self.emit(f"mov {dest_reg}, rax")
                else:
                    dest_reg = self.arg_regs_32[i]
                    if dest_reg != 'eax':
                        self.emit(f"mov {dest_reg}, eax")
        
        # Call the function
        self.emit(f"call {call.name}")
        
        # Clean up stack args if any
        if stack_args > 0:
            cleanup_bytes = stack_args * 8
            if need_alignment_padding:
                cleanup_bytes += 8
            self.emit(f"add rsp, {cleanup_bytes}")
        elif need_alignment_padding:
            self.emit("add rsp, 8  ; remove alignment padding")
        
        # Result is in eax/rax
        # Note: update params are already written back by the callee via stored pointers
    
    # ==========================================================================
    # READ SYSCALL IMPLEMENTATIONS
    # ==========================================================================
    
    def compile_read(self, read_expr: 'Read'):
        """
        read() - read until EOF
        Uses mmap to allocate buffer, reads into it
        Result: rax = pointer to string (for str) or parsed integer
        """
        target_type = getattr(read_expr, 'target_type', 'str')
        self.needs_read_failed_flag = True
        
        if target_type == 'str':
            self.compile_read_string_until_eof()
        else:
            # Read until EOF, parse as integer
            self.compile_read_integer_until_eof(target_type)
    
    def compile_readln(self, readln_expr: 'Readln'):
        """
        readln() - read one line until \\n
        Uses mmap to allocate buffer, reads into it
        Result: rax = pointer to string (for str) or parsed integer
        """
        target_type = getattr(readln_expr, 'target_type', 'str')
        self.needs_read_failed_flag = True
        
        if target_type == 'str':
            self.compile_readln_string()
        else:
            # Read line, parse as integer
            self.compile_readln_integer(target_type)
    
    def compile_readchar(self, readchar_expr: 'Readchar'):
        """
        readchar() - read single byte
        Returns i32: byte value (0-255) or -1 for EOF
        """
        self.needs_read_failed_flag = True
        
        self.emit("; readchar() - read single byte")
        self.emit("sub rsp, 8")          # Allocate 1 byte on stack (aligned)
        
        # read(0, rsp, 1)
        self.emit("xor eax, eax")        # syscall: read = 0
        self.emit("xor edi, edi")        # fd: stdin = 0
        self.emit("mov rsi, rsp")        # buffer: stack
        self.emit("mov edx, 1")          # count: 1 byte
        self.emit("syscall")
        
        # Check result
        self.emit("test rax, rax")
        done_label = self.fresh_label("readchar_done")
        
        # If rax <= 0, EOF or error
        self.emit(f"jle .readchar_eof_{self.label_counter}")
        
        # Success: load byte, clear error flag
        self.emit("movzx eax, byte [rsp]")  # Load byte into eax (zero-extended)
        self.emit_set_read_failed(0)
        self.emit(f"jmp {done_label}")
        
        # EOF/error case
        self.emit_label(f".readchar_eof_{self.label_counter}")
        self.emit("mov eax, -1")         # Return -1 for EOF
        self.emit_set_read_failed(1)
        
        self.emit_label(done_label)
        self.emit("add rsp, 8")          # Restore stack
    
    def compile_read_failed(self, read_failed_expr: 'ReadFailed'):
        """
        read_failed() - returns bool indicating if last read failed
        """
        self.needs_read_failed_flag = True
        
        self.emit("; read_failed() - check error flag")
        self.emit("movabs r11, @_read_failed")
        self.emit("movzx eax, byte [r11]")
    
    def emit_set_read_failed(self, value: int):
        """Helper to set the _read_failed flag (0 or 1)"""
        self.emit(f"movabs r11, @_read_failed")
        self.emit(f"mov byte [r11], {value}")
    
    def emit_set_read_failed_from_al(self):
        """Helper to set the _read_failed flag from AL register"""
        self.emit("movabs r11, @_read_failed")
        self.emit("mov byte [r11], al")
    
    def compile_read_string_until_eof(self):
        """
        Read all input until EOF into mmap'd buffer
        Returns pointer in rax
        """
        buffer_size = 4096  # Initial buffer size
        
        self.emit("; read() string - read until EOF")
        self.emit("push rbx")
        self.emit("push r12")
        self.emit("push r13")
        self.emit("push r14")
        
        # mmap anonymous memory for buffer
        # mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
        self.emit(f"; mmap buffer ({buffer_size} bytes)")
        self.emit("mov rax, 9")           # syscall: mmap
        self.emit("xor rdi, rdi")         # addr: NULL
        self.emit(f"mov rsi, {buffer_size}")  # length
        self.emit("mov rdx, 3")           # prot: PROT_READ | PROT_WRITE
        self.emit("mov r10, 0x22")        # flags: MAP_PRIVATE | MAP_ANONYMOUS
        self.emit("mov r8, -1")           # fd: -1
        self.emit("xor r9, r9")           # offset: 0
        self.emit("syscall")
        
        self.emit("mov r12, rax")         # r12 = buffer start
        self.emit("mov r13, rax")         # r13 = current write position
        self.emit(f"mov r14, {buffer_size}")  # r14 = remaining space
        
        read_loop = self.fresh_label("read_loop")
        read_done = self.fresh_label("read_done")
        
        self.emit_label(read_loop)
        # read(0, r13, r14)
        self.emit("xor eax, eax")         # syscall: read
        self.emit("xor edi, edi")         # fd: stdin
        self.emit("mov rsi, r13")         # buf: current position
        self.emit("mov rdx, r14")         # count: remaining
        self.emit("syscall")
        
        # Check result
        self.emit("test rax, rax")
        self.emit(f"jle {read_done}")     # EOF or error
        
        # Update position
        self.emit("add r13, rax")         # advance position
        self.emit("sub r14, rax")         # decrease remaining
        
        # If buffer full, we'd need to grow - for MVP just continue
        self.emit("test r14, r14")
        self.emit(f"jnz {read_loop}")
        
        self.emit_label(read_done)
        # Calculate length and null-terminate
        self.emit("mov byte [r13], 0")    # null terminate
        
        # Set error flag based on whether we read anything
        self.emit("mov rax, r13")
        self.emit("sub rax, r12")         # rax = bytes read
        self.emit("test rax, rax")
        self.emit("setz al")              # al = 1 if no bytes read
        self.emit_set_read_failed_from_al()
        
        # Return buffer pointer
        self.emit("mov rax, r12")
        
        self.emit("pop r14")
        self.emit("pop r13")
        self.emit("pop r12")
        self.emit("pop rbx")
    
    def compile_readln_string(self):
        """
        Read one line (until \\n) into mmap'd buffer
        Strips the newline character
        Returns pointer in rax
        """
        buffer_size = 4096
        
        self.emit("; readln() string - read until newline")
        self.emit("push rbx")
        self.emit("push r12")
        self.emit("push r13")
        
        # mmap anonymous memory for buffer
        self.emit(f"; mmap buffer ({buffer_size} bytes)")
        self.emit("mov rax, 9")           # syscall: mmap
        self.emit("xor rdi, rdi")         # addr: NULL
        self.emit(f"mov rsi, {buffer_size}")  # length
        self.emit("mov rdx, 3")           # prot: PROT_READ | PROT_WRITE
        self.emit("mov r10, 0x22")        # flags: MAP_PRIVATE | MAP_ANONYMOUS
        self.emit("mov r8, -1")           # fd: -1
        self.emit("xor r9, r9")           # offset: 0
        self.emit("syscall")
        
        self.emit("mov r12, rax")         # r12 = buffer start
        self.emit("mov r13, rax")         # r13 = current write position
        self.emit("xor rbx, rbx")         # rbx = bytes read total
        
        read_loop = self.fresh_label("readln_loop")
        read_done = self.fresh_label("readln_done")
        read_eof = self.fresh_label("readln_eof")
        
        self.emit_label(read_loop)
        # read(0, r13, 1) - read one byte at a time
        self.emit("xor eax, eax")         # syscall: read
        self.emit("xor edi, edi")         # fd: stdin
        self.emit("mov rsi, r13")         # buf: current position
        self.emit("mov edx, 1")           # count: 1 byte
        self.emit("syscall")
        
        # Check result
        self.emit("test rax, rax")
        self.emit(f"jle {read_eof}")      # EOF or error
        
        # Check if newline - load byte first then compare
        self.emit("movzx eax, byte [r13]")  # load byte into eax
        self.emit("cmp eax, 10")          # compare with '\\n'
        self.emit(f"je {read_done}")
        
        # Not newline, advance
        self.emit("inc r13")
        self.emit("inc rbx")
        self.emit(f"jmp {read_loop}")
        
        self.emit_label(read_eof)
        # EOF reached - check if we read anything
        self.emit("test rbx, rbx")
        self.emit("xor eax, eax")         # clear eax
        self.emit(f"jnz .readln_have_data_{self.label_counter}")
        self.emit("mov eax, 1")           # no data = error
        self.emit_label(f".readln_have_data_{self.label_counter}")
        self.emit_set_read_failed_from_al()
        self.emit(f"jmp .readln_finish_{self.label_counter}")
        
        self.emit_label(read_done)
        # Newline found - success, clear error flag
        self.emit_set_read_failed(0)
        
        self.emit_label(f".readln_finish_{self.label_counter}")
        # Null-terminate (at current position, overwriting newline or at end)
        self.emit("mov byte [r13], 0")
        
        # Return buffer pointer
        self.emit("mov rax, r12")
        
        self.emit("pop r13")
        self.emit("pop r12")
        self.emit("pop rbx")
    
    def compile_readln_integer(self, target_type: str):
        """
        Read one line and parse as integer
        Sets read_failed if parsing fails
        Returns parsed value in rax/eax
        """
        is_signed = target_type in ('i8', 'i16', 'i32', 'i64')
        
        self.emit(f"; readln() integer ({target_type})")
        self.emit("push rbx")
        self.emit("push r12")
        self.emit("push r13")
        self.emit("push r14")
        
        # Use stack for small buffer (64 bytes enough for any integer)
        self.emit("sub rsp, 64")
        
        self.emit("mov r12, rsp")         # r12 = buffer start
        self.emit("mov r13, rsp")         # r13 = current position
        self.emit("xor r14, r14")         # r14 = bytes read
        
        read_loop = self.fresh_label("readln_int_loop")
        parse_start = self.fresh_label("parse_start")
        read_eof = self.fresh_label("readln_int_eof")
        
        self.emit_label(read_loop)
        # read(0, r13, 1)
        self.emit("xor eax, eax")
        self.emit("xor edi, edi")
        self.emit("mov rsi, r13")
        self.emit("mov edx, 1")
        self.emit("syscall")
        
        self.emit("test rax, rax")
        self.emit(f"jle {read_eof}")
        
        # Check for newline (load byte first, then compare)
        self.emit("movzx eax, byte [r13]")
        self.emit("cmp al, 10")
        self.emit(f"je {parse_start}")
        
        # Advance
        self.emit("inc r13")
        self.emit("inc r14")
        self.emit("cmp r14, 63")          # Buffer limit
        self.emit(f"jl {read_loop}")
        
        self.emit_label(read_eof)
        # EOF - if no bytes read, error
        self.emit("test r14, r14")
        self.emit(f"jnz {parse_start}")
        # Empty input
        self.emit("xor eax, eax")
        self.emit_set_read_failed(1)
        self.emit(f"jmp .readln_int_done_{self.label_counter}")
        
        self.emit_label(parse_start)
        # Null-terminate
        self.emit("mov byte [r13], 0")
        
        # Parse integer from buffer at r12
        self._emit_parse_integer(target_type, is_signed)
        
        self.emit_label(f".readln_int_done_{self.label_counter}")
        self.emit("add rsp, 64")
        self.emit("pop r14")
        self.emit("pop r13")
        self.emit("pop r12")
        self.emit("pop rbx")
    
    def compile_read_integer_until_eof(self, target_type: str):
        """
        Read until EOF and parse as integer (takes first valid integer)
        For simplicity, this is same as readln for MVP
        """
        self.compile_readln_integer(target_type)
    
    def _emit_parse_integer(self, target_type: str, is_signed: bool):
        """
        Parse integer from null-terminated string at r12
        Result in rax, sets read_failed on error
        """
        parse_loop = self.fresh_label("parse_loop")
        parse_done = self.fresh_label("parse_done")
        parse_error = self.fresh_label("parse_error")
        
        self.emit("; parse integer from string")
        self.emit("xor rax, rax")         # result = 0
        self.emit("xor rbx, rbx")         # negative flag = 0
        self.emit("mov r13, r12")         # r13 = current char
        
        # Skip leading whitespace
        skip_ws = self.fresh_label("skip_ws")
        self.emit_label(skip_ws)
        self.emit("movzx ecx, byte [r13]")
        self.emit("cmp cl, ' '")
        self.emit(f"jne .check_sign_{self.label_counter}")
        self.emit("inc r13")
        self.emit(f"jmp {skip_ws}")
        
        self.emit_label(f".check_sign_{self.label_counter}")
        # Check for sign
        if is_signed:
            self.emit("cmp cl, '-'")
            self.emit(f"jne .check_plus_{self.label_counter}")
            self.emit("mov rbx, 1")       # negative = true
            self.emit("inc r13")
            self.emit(f"jmp {parse_loop}")
            
            self.emit_label(f".check_plus_{self.label_counter}")
            self.emit("cmp cl, '+'")
            self.emit(f"jne {parse_loop}")
            self.emit("inc r13")
        
        self.emit_label(parse_loop)
        self.emit("movzx ecx, byte [r13]")
        
        # Check for end (null or whitespace)
        self.emit("test cl, cl")
        self.emit(f"jz {parse_done}")
        self.emit("cmp cl, ' '")
        self.emit(f"je {parse_done}")
        self.emit("cmp cl, 10")           # newline
        self.emit(f"je {parse_done}")
        self.emit("cmp cl, 13")           # carriage return
        self.emit(f"je {parse_done}")
        
        # Check if digit
        self.emit("sub cl, '0'")
        self.emit("cmp cl, 9")
        self.emit(f"ja {parse_error}")    # Not a digit
        
        # result = result * 10 + digit
        # Use r14 as scratch for multiply: rax = rax * 10
        self.emit("mov r14, 10")
        self.emit("imul rax, r14")
        # cl already has the digit, extend to r14 and add
        self.emit("and rcx, 0xFF")        # Zero-extend cl to rcx
        self.emit("add rax, rcx")
        
        self.emit("inc r13")
        self.emit(f"jmp {parse_loop}")
        
        self.emit_label(parse_error)
        self.emit("xor eax, eax")
        self.emit_set_read_failed(1)
        self.emit(f"jmp .parse_return_{self.label_counter}")
        
        self.emit_label(parse_done)
        # Check if we parsed at least one digit
        self.emit("cmp r13, r12")
        self.emit(f"jle {parse_error}")   # No digits parsed
        
        # Apply negative sign
        if is_signed:
            self.emit("test rbx, rbx")
            self.emit(f"jz .parse_positive_{self.label_counter}")
            self.emit("neg rax")
            self.emit_label(f".parse_positive_{self.label_counter}")
        
        self.emit_set_read_failed(0)  # Success
        
        self.emit_label(f".parse_return_{self.label_counter}")


if __name__ == '__main__':
    from tokenization_engine import Lexer
    from syntactic_analyzer import Parser
    from semantic_analyzer import SemanticAnalyzer
    
    print("=" * 70)
    print("AXIS Code Generator - Tests")
    print("=" * 70)
    print()
    
    # Test 1: Simple locals
    print("Test 1: Simple locals with arithmetic")
    print("-" * 70)
    source1 = """
    fn main() -> i32 {
        let x: i32 = 10;
        let y: i32 = 20;
        return x + y;
    }
    """
    
    lexer1 = Lexer(source1)
    tokens1 = lexer1.tokenize()
    parser1 = Parser(tokens1)
    ast1 = parser1.parse()
    analyzer1 = SemanticAnalyzer()
    analyzer1.analyze(ast1)
    
    codegen1 = CodeGenerator()
    asm1 = codegen1.compile(ast1)
    print(asm1)
    print()
    
    # Test 2: If/While
    print("Test 2: Control Flow (if/while)")
    print("-" * 70)
    source2 = """
    fn main() -> i32 {
        let mut i: i32 = 0;
        while i < 3 {
            i = i + 1;
        }
        if i == 3 {
            return 1;
        }
        return 0;
    }
    """
    
    lexer2 = Lexer(source2)
    tokens2 = lexer2.tokenize()
    parser2 = Parser(tokens2)
    ast2 = parser2.parse()
    analyzer2 = SemanticAnalyzer()
    analyzer2.analyze(ast2)
    
    codegen2 = CodeGenerator()
    asm2 = codegen2.compile(ast2)
    print(asm2)
    print()
