"""
AXIS Code Generator - x86-64 Backend
Transformiert typisierten AST in x86-64 Assembly über Assembler-Backend.
"""

import re
from typing import List, Optional
from syntactic_analyzer import *
from semantic_analyzer import SemanticAnalyzer


class CodeGenerator:
    """
    x86-64 Code Generator für AXIS
    
    Calling Convention: System V AMD64 (Linux)
    - Args: rdi, rsi, rdx, rcx, r8, r9 (für i32: edi, esi, edx, ecx, r8d, r9d)
    - Return: rax (für i32: eax)
    - Callee-saved: rbx, rbp, r12-r15
    - Caller-saved: alle anderen
    
    Expression Evaluation:
    - Ergebnis in eax/rax
    - Temp-Register: ecx, edx
    
    Stack-Layout:
    - [rbp+0]   = saved rbp
    - [rbp-4]   = erste lokale Variable (i32)
    - [rbp-8]   = zweite lokale Variable
    - ...
    """
    
    def __init__(self):
        self.asm_lines: List[str] = []
        self.label_counter = 0
        self.current_function = None
        self.loop_stack = []  # Stack für break/continue: (continue_label, break_label)
        
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
        
        # Generiere Code für alle Funktionen
        for func in program.functions:
            self.compile_function(func)
        
        return '\n'.join(self.asm_lines)
    
    def compile_function(self, func: Function):
        self.current_function = func
        
        # Function Label
        self.emit_label(func.name)
        
        # Prolog
        self.emit("push rbp")
        self.emit("mov rbp, rsp")
        
        # Stack-Space allokieren
        if func.stack_size > 0:
            self.emit(f"sub rsp, {func.stack_size}")
        
        self.compile_block(func.body)
        
        # Epilog (falls kein explizites Return)
        self.emit_label(f"{func.name}_epilog")
        if func.stack_size > 0:
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
        elif isinstance(stmt, Return):
            self.compile_return(stmt)
        elif isinstance(stmt, If):
            self.compile_if(stmt)
        elif isinstance(stmt, While):
            self.compile_while(stmt)
        elif isinstance(stmt, Break):
            self.compile_break(stmt)
        elif isinstance(stmt, Continue):
            self.compile_continue(stmt)
        elif isinstance(stmt, ExprStatement):
            self.compile_expression(stmt.expression)
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
            elif var_type in ['i64', 'u64']:
                self.emit(f"mov qword [rbp{vardecl.stack_offset:+d}], rax")
            else:
                self.emit(f"mov [rbp{vardecl.stack_offset:+d}], eax")
    
    def compile_assignment(self, assign: Assignment):
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
        elif symbol.type in ['i64', 'u64']:
            self.emit(f"mov qword [rbp{symbol.stack_offset:+d}], rax")
        else:
            self.emit(f"mov [rbp{symbol.stack_offset:+d}], eax")
    
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
    
    def compile_expression(self, expr: Expression):
        # expression compilation - result landet immer in eax
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
        
        if symbol.is_param:
            raise NotImplementedError("Parameter access not yet implemented in MVP")
        else:
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
            elif symbol.type in ['i64', 'u64']:
                # Full 64-bit load
                self.emit(f"mov rax, qword [rbp{symbol.stack_offset:+d}]")
            else:
                # Default i32 load
                self.emit(f"mov eax, [rbp{symbol.stack_offset:+d}]")
    
    def compile_binaryop(self, binop: BinaryOp):
        # binary operations - beide seiten evaluate und dann op
        if binop.op in ['+', '-', '*', '/', '%', '&', '|', '^']:
            self.compile_expression(binop.left)
            
            # rechts in temp register
            self.emit("push rax")
            self.compile_expression(binop.right)
            self.emit("mov ecx, eax")
            self.emit("pop rax")
            
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
            self.compile_expression(binop.left)
            
            # rechts in ecx (shift count)
            self.emit("push rax")
            self.compile_expression(binop.right)
            self.emit("mov ecx, eax")
            self.emit("pop rax")
            
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
            # Links -> eax
            self.compile_expression(binop.left)
            
            # Rechts -> ecx
            self.emit("push rax")
            self.compile_expression(binop.right)
            self.emit("mov ecx, eax")
            self.emit("pop rax")
            
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
        elif unaryop.op == '!':
            # Boolean NOT: flip 0 to 1 and 1 to 0
            self.compile_expression(unaryop.operand)
            self.emit("xor eax, 1")  # Toggle lowest bit
        else:
            raise NotImplementedError(f"Unary operator '{unaryop.op}' not implemented")
    
    def compile_call(self, call: Call):
        # System V AMD64: Args in rdi, rsi, rdx, rcx, r8, r9
        # Für i32: edi, esi, edx, ecx, r8d, r9d
        
        if len(call.args) > 6:
            raise NotImplementedError("More than 6 arguments not supported in MVP")
        
        # Evaluiere Argumente und lade in Register
        # Evaluate all args and push to stack first to avoid clobbering
        for arg in call.args:
            self.compile_expression(arg)
            self.emit("push rax")
        
        # Pop args in reverse order into argument registers
        for i in range(len(call.args) - 1, -1, -1):
            self.emit("pop rax")
            dest_reg = self.arg_regs_32[i]
            if dest_reg != 'eax':
                self.emit(f"mov {dest_reg}, eax")
        
        # Call
        self.emit(f"call {call.name}")
        
        # Ergebnis ist bereits in eax

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
