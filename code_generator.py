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
        """Adds Assembly-Zeile """
        self.asm_lines.append(line)
    
    def emit_label(self, label: str):
        """Adds Label """
        self.asm_lines.append(f"{label}:")
    
    def fresh_label(self, prefix: str = "L") -> str:
        """Generates einzigartiges Label"""
        label = f"{prefix}_{self.label_counter}"
        self.label_counter += 1
        return label
    
    def compile(self, program: Program) -> str:
        """Compiles Program-AST zu Assembly-String"""
        self.asm_lines = []
        self.label_counter = 0
        
        # Generiere Code für alle Funktionen
        for func in program.functions:
            self.compile_function(func)
        
        return '\n'.join(self.asm_lines)
    
    # ========================================================================
    # Function Compilation
    # ========================================================================
    
    def compile_function(self, func: Function):
        """Compiles Funktion"""
        self.current_function = func
        
        # Function Label
        self.emit_label(func.name)
        
        # Prolog
        self.emit("push rbp")
        self.emit("mov rbp, rsp")
        
        # Stack-Space allokieren
        if func.stack_size > 0:
            self.emit(f"sub rsp, {func.stack_size}")
        
        # Parameter in Stack-Slots laden (für MVP: nur Register-Args)
        # Semantic Analyzer markiert Params mit is_param=True
        # Wir müssen jetzt die first 6 Args aus Registern laden
        # TODO: Implementiere Parameter-Handling
        # for now: Params bleiben in Registern (keine lokalen Vars mit gleichem Namen)
        
        # Body kompilieren
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
        """Compiles Block"""
        for stmt in block.statements:
            self.compile_statement(stmt)
    
    # ========================================================================
    # Statement Compilation
    # ========================================================================
    
    def compile_statement(self, stmt: Statement):
        """Compiles Statement"""
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
        """Compiles Variable Declaration"""
        if vardecl.init:
            # Init-Expression evaluieren -> eax
            self.compile_expression(vardecl.init)
            
            # Store zu Stack-Slot
            # vardecl.stack_offset ist negativ (z.B. -4)
            self.emit(f"mov [rbp{vardecl.stack_offset:+d}], eax")
    
    def compile_assignment(self, assign: Assignment):
        """Compiles Assignment"""
        # Value evaluieren -> eax
        self.compile_expression(assign.value)
        
        # Target muss Identifier sein
        if not isinstance(assign.target, Identifier):
            raise NotImplementedError("Assignment target must be identifier")
        
        # Store zu Stack-Slot
        symbol = assign.target.symbol
        self.emit(f"mov [rbp{symbol.stack_offset:+d}], eax")
    
    def compile_return(self, ret: Return):
        """Compiles Return"""
        if ret.value:
            # Value evaluieren -> eax
            self.compile_expression(ret.value)
        
        # Jump zu Epilog
        self.emit(f"jmp {self.current_function.name}_epilog")
    
    def compile_if(self, if_stmt: If):
        """Compiles If-Statement"""
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
        """Compiles While-Loop"""
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
        """Compiles Break"""
        if not self.loop_stack:
            raise RuntimeError("Break outside of loop")
        
        _, break_label = self.loop_stack[-1]
        self.emit(f"jmp {break_label}")
    
    def compile_continue(self, continue_stmt: Continue):
        """Compiles Continue"""
        if not self.loop_stack:
            raise RuntimeError("Continue outside of loop")
        
        continue_label, _ = self.loop_stack[-1]
        self.emit(f"jmp {continue_label}")
    
    # ========================================================================
    # Expression Compilation (Result in eax/rax)
    # ========================================================================
    
    def compile_expression(self, expr: Expression):
        """Compiles Expression -> eax"""
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
        """Compiles Literal -> eax"""
        # Parse Immediate
        value = self.parse_literal_value(lit.value)
        self.emit(f"mov eax, {value}")
    
    def parse_literal_value(self, value_str: str) -> int:
        """Parses Literal-value (hex, binary, decimal)"""
        value_str = value_str.strip()
        
        if value_str.startswith('0x') or value_str.startswith('0X'):
            return int(value_str, 16)
        elif value_str.startswith('0b') or value_str.startswith('0B'):
            return int(value_str, 2)
        else:
            return int(value_str, 10)
    
    def compile_identifier(self, ident: Identifier):
        """Compiles Identifier -> eax"""
        symbol = ident.symbol
        
        if symbol.is_param:
            # Parameter: Für MVP in Register (erste 6)
            # TODO: Implementiere Parameter-Mapping
            # for now: Error
            raise NotImplementedError("Parameter access not yet implemented in MVP")
        else:
            # Lokale Variable: Load von Stack
            self.emit(f"mov eax, [rbp{symbol.stack_offset:+d}]")
    
    def compile_binaryop(self, binop: BinaryOp):
        """Compiles Binary Operation -> eax"""
        # Arithmetik: + - * /
        if binop.op in ['+', '-']:
            # Links -> eax
            self.compile_expression(binop.left)
            
            # Rechts -> ecx (temp)
            self.emit("push rax")  # Save left
            self.compile_expression(binop.right)
            self.emit("mov ecx, eax")  # right in ecx
            self.emit("pop rax")  # Restore left
            
            if binop.op == '+':
                self.emit("add eax, ecx")
            elif binop.op == '-':
                self.emit("sub eax, ecx")
        
        elif binop.op in ['*', '/']:
            # Multiplikation/Division: not implemented im MVP
            raise NotImplementedError(f"Operator '{binop.op}' not implemented in MVP (requires imul/idiv)")
        
        # Vergleiche: == != < <= > >=
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
        """Compiles Unary Operation -> eax"""
        if unaryop.op == '-':
            # Negation
            self.compile_expression(unaryop.operand)
            self.emit("neg eax")
        else:
            raise NotImplementedError(f"Unary operator '{unaryop.op}' not implemented")
    
    def compile_call(self, call: Call):
        """Compiles Function Call -> eax"""
        # System V AMD64: Args in rdi, rsi, rdx, rcx, r8, r9
        # Für i32: edi, esi, edx, ecx, r8d, r9d
        
        if len(call.args) > 6:
            raise NotImplementedError("More than 6 arguments not supported in MVP")
        
        # Evaluiere Argumente und lade in Register
        # TODO: Für mehrere Args brauchen wir Stack-based eval
        # MVP: Max 6 Args, evaluiere von links nach rechts
        
        for i, arg in enumerate(call.args):
            # Evaluiere arg -> eax
            self.compile_expression(arg)
            
            # Move zu Argument-Register
            if i < len(self.arg_regs_32):
                dest_reg = self.arg_regs_32[i]
                if dest_reg != 'eax':  # Avoid redundant mov
                    self.emit(f"mov {dest_reg}, eax")
        
        # Call
        self.emit(f"call {call.name}")
        
        # Ergebnis ist bereits in eax


# ============================================================================
# Test
# ============================================================================

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
