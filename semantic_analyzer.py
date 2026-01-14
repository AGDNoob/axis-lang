"""
AXIS Semantic Analyzer
Type checking, Symbol table, Stack layout calculation.
Keine Codegen - nur AST-Annotation.
"""

from dataclasses import dataclass, field
from typing import Optional, Dict
from syntactic_analyzer import *


# ============================================================================
# Type System
# ============================================================================

TYPE_SIZES = {
    'i8': 1, 'i16': 2, 'i32': 4, 'i64': 8,
    'u8': 1, 'u16': 2, 'u32': 4, 'u64': 8,
    'ptr': 8,
    'bool': 1,
}

SIGNED_TYPES = {'i8', 'i16', 'i32', 'i64'}
UNSIGNED_TYPES = {'u8', 'u16', 'u32', 'u64'}
INTEGER_TYPES = SIGNED_TYPES | UNSIGNED_TYPES
VALID_TYPES = INTEGER_TYPES | {'ptr', 'bool'}


def is_integer_type(type_name: str) -> bool:
    """Checks ob Typ ein Integer ist"""
    return type_name in INTEGER_TYPES


def is_pointer_type(type_name: str) -> bool:
    """Checks ob Typ ein Pointer ist"""
    return type_name == 'ptr'


def get_type_size(type_name: str) -> int:
    """Returns size eines Typs in Bytes """
    if type_name not in TYPE_SIZES:
        raise SemanticError(f"Unknown type: {type_name}")
    return TYPE_SIZES[type_name]


def align_offset(offset: int, alignment: int) -> int:
    """Aligned Offset berechnen (aufrunden)"""
    return ((offset + alignment - 1) // alignment) * alignment


# ============================================================================
# Symbol und Scope
# ============================================================================

@dataclass
class Symbol:
    """Symbol in der Symbol table"""
    name: str
    type: str
    mutable: bool
    stack_offset: int = 0
    is_param: bool = False
    
    def __repr__(self):
        mut = "mut " if self.mutable else ""
        param = " [param]" if self.is_param else ""
        return f"Symbol({mut}{self.name}: {self.type} @ rbp{self.stack_offset:+d}{param})"


@dataclass
class FunctionSymbol:
    """Funktions-Symbol"""
    name: str
    params: list[tuple[str, str]]  # (name, type)
    return_type: Optional[str]
    
    def __repr__(self):
        params_str = ", ".join(f"{name}: {type_}" for name, type_ in self.params)
        ret_str = f" -> {self.return_type}" if self.return_type else ""
        return f"Function({self.name}({params_str}){ret_str})"


class Scope:
    """Lexical scope mit Parent-chaining"""
    
    def __init__(self, parent: Optional['Scope'] = None):
        self.parent = parent
        self.symbols: Dict[str, Symbol] = {}
    
    def define(self, symbol: Symbol):
        """Defines Symbol im current Scope"""
        if symbol.name in self.symbols:
            raise SemanticError(f"Symbol '{symbol.name}' already defined in this scope")
        self.symbols[symbol.name] = symbol
    
    def lookup(self, name: str) -> Optional[Symbol]:
        """Searches Symbol im current oder Parent-Scope"""
        if name in self.symbols:
            return self.symbols[name]
        if self.parent:
            return self.parent.lookup(name)
        return None
    
    def lookup_local(self, name: str) -> Optional[Symbol]:
        """Searches Symbol nur im current Scope"""
        return self.symbols.get(name)


# ============================================================================
# Semantic Error
# ============================================================================

class SemanticError(Exception):
    """Semantic error"""
    pass


# ============================================================================
# Semantic Analyzer
# ============================================================================

class SemanticAnalyzer:
    """
    Semantic Analyzer:
    - Builds Symbol table
    - Typisiert Expressions
    - Calculates Stack-Layout
    - Annotiert AST
    """
    
    def __init__(self):
        self.global_scope: Optional[Scope] = None
        self.current_scope: Optional[Scope] = None
        self.functions: Dict[str, FunctionSymbol] = {}
        self.current_function: Optional[Function] = None
        self.current_stack_offset: int = 0
        self.in_loop: int = 0  # Zähler für verschachtelte Loops
    
    def error(self, msg: str):
        """Throws semantischen Fehler"""
        raise SemanticError(msg)
    
    def enter_scope(self):
        """Creates neuen Scope"""
        self.current_scope = Scope(parent=self.current_scope)
    
    def exit_scope(self):
        """Exits current Scope"""
        if not self.current_scope:
            self.error("Cannot exit scope: no current scope")
        self.current_scope = self.current_scope.parent
    
    def define_symbol(self, name: str, type_: str, mutable: bool, is_param: bool = False) -> Symbol:
        """Defines Symbol im current Scope"""
        if not self.current_scope:
            self.error("No current scope")
        
        # Prüfe Typ-Validität
        if type_ not in VALID_TYPES:
            self.error(f"Unknown type: {type_}")
        
        # Berechne Stack-Offset
        type_size = get_type_size(type_)
        
        if is_param:
            # Parameter: Erstmal Stack-Offset 0, wird später von Calling Convention gesetzt
            stack_offset = 0
        else:
            # Lokale Variable: Stack grows downward
            # Alignment: Typen sind selbst-aligned (8-byte Typ → 8-byte aligned)
            alignment = min(type_size, 8)
            self.current_stack_offset = align_offset(self.current_stack_offset, alignment)
            self.current_stack_offset += type_size
            stack_offset = -self.current_stack_offset
        
        symbol = Symbol(name, type_, mutable, stack_offset, is_param)
        self.current_scope.define(symbol)
        return symbol
    
    def lookup_symbol(self, name: str) -> Symbol:
        """Searches Symbol"""
        if not self.current_scope:
            self.error("No current scope")
        
        symbol = self.current_scope.lookup(name)
        if not symbol:
            self.error(f"Undefined variable: {name}")
        return symbol
    
    def lookup_function(self, name: str) -> FunctionSymbol:
        """Searches Funktion"""
        if name not in self.functions:
            self.error(f"Undefined function: {name}")
        return self.functions[name]
    
    # ========================================================================
    # Analyze Entry Point
    # ========================================================================
    
    def analyze(self, program: Program):
        """Analyzes komplettes Programm"""
        # Global Scope erstellen
        self.global_scope = Scope()
        self.current_scope = self.global_scope
        
        # Pass 1: Sammle alle Funktions-Signaturen
        for func in program.functions:
            if func.name in self.functions:
                self.error(f"Duplicate function definition: {func.name}")
            
            func_symbol = FunctionSymbol(func.name, func.params, func.return_type)
            self.functions[func.name] = func_symbol
        
        # Pass 2: Analysiere Funktions-Bodies
        for func in program.functions:
            self.analyze_function(func)
    
    def analyze_function(self, func: Function):
        """Analyzes Funktion"""
        self.current_function = func
        self.current_stack_offset = 0
        
        # Neuer Scope für Funktion
        self.enter_scope()
        
        # Parameter definieren
        for param_name, param_type in func.params:
            # Parameter sind immer immutable (per Design-Entscheidung)
            self.define_symbol(param_name, param_type, mutable=False, is_param=True)
        
        # Body analysieren
        self.analyze_block(func.body)
        
        # Stack-Size berechnen (16-byte aligned für Call-Boundary)
        stack_size = align_offset(self.current_stack_offset, 16)
        
        # AST annotieren
        func.stack_size = stack_size
        func.analyzed = True
        
        self.exit_scope()
        self.current_function = None
    
    def analyze_block(self, block: Block):
        """Analyzes Block"""
        # Neuer Scope für Block
        self.enter_scope()
        
        for stmt in block.statements:
            self.analyze_statement(stmt)
        
        self.exit_scope()
    
    # ========================================================================
    # Statement Analysis
    # ========================================================================
    
    def analyze_statement(self, stmt: Statement):
        """Analyzes Statement"""
        if isinstance(stmt, VarDecl):
            self.analyze_vardecl(stmt)
        elif isinstance(stmt, Assignment):
            self.analyze_assignment(stmt)
        elif isinstance(stmt, Return):
            self.analyze_return(stmt)
        elif isinstance(stmt, If):
            self.analyze_if(stmt)
        elif isinstance(stmt, While):
            self.analyze_while(stmt)
        elif isinstance(stmt, Break):
            self.analyze_break(stmt)
        elif isinstance(stmt, Continue):
            self.analyze_continue(stmt)
        elif isinstance(stmt, ExprStatement):
            self.analyze_expression(stmt.expression)
        else:
            self.error(f"Unknown statement type: {type(stmt).__name__}")
    
    def analyze_vardecl(self, vardecl: VarDecl):
        """Analyzes Variable Declaration"""
        # Typ validieren
        if vardecl.type not in VALID_TYPES:
            self.error(f"Unknown type: {vardecl.type}")
        
        # Init-Expression typisieren (If present)
        if vardecl.init:
            init_type = self.analyze_expression(vardecl.init)
            
            # Type-Check: init muss vom gleichen Typ sein
            if init_type != vardecl.type:
                self.error(f"Type mismatch in variable '{vardecl.name}': expected {vardecl.type}, got {init_type}")
        
        # Symbol definieren
        symbol = self.define_symbol(vardecl.name, vardecl.type, vardecl.mutable)
        
        # AST annotieren
        vardecl.stack_offset = symbol.stack_offset
        vardecl.symbol = symbol
    
    def analyze_assignment(self, assign: Assignment):
        """Analyzes Assignment"""
        # Target muss Identifier sein (oder später: Deref)
        if not isinstance(assign.target, Identifier):
            self.error("Assignment target must be an identifier")
        
        # Symbol lookup
        symbol = self.lookup_symbol(assign.target.name)
        
        # Mutable-Check
        if not symbol.mutable:
            self.error(f"Cannot assign to immutable variable: {symbol.name}")
        
        # Type-Check
        target_type = symbol.type
        value_type = self.analyze_expression(assign.value)
        
        if target_type != value_type:
            self.error(f"Type mismatch in assignment to '{symbol.name}': expected {target_type}, got {value_type}")
        
        # AST annotieren
        assign.target.symbol = symbol
        assign.target.inferred_type = target_type
    
    def analyze_return(self, ret: Return):
        """Analyzes Return"""
        if not self.current_function:
            self.error("Return outside of function")
        
        # Type-Check
        if ret.value:
            value_type = self.analyze_expression(ret.value)
            
            if not self.current_function.return_type:
                self.error(f"Function '{self.current_function.name}' has no return type but returns a value")
            
            if value_type != self.current_function.return_type:
                self.error(f"Return type mismatch: expected {self.current_function.return_type}, got {value_type}")
        else:
            if self.current_function.return_type:
                self.error(f"Function '{self.current_function.name}' must return a value of type {self.current_function.return_type}")
    
    def analyze_if(self, if_stmt: If):
        """Analyzes If-Statement"""
        # Condition muss bool sein (oder Integer for now)
        cond_type = self.analyze_expression(if_stmt.condition)
        # TODO: Strikte bool-Prüfung optional
        
        # Then-Block analysieren
        self.analyze_block(if_stmt.then_block)
        
        # Else-Block analysieren
        if if_stmt.else_block:
            self.analyze_block(if_stmt.else_block)
    
    def analyze_while(self, while_stmt: While):
        """Analyzes While-Statement"""
        # Condition typisieren
        cond_type = self.analyze_expression(while_stmt.condition)
        
        # Body analysieren (in Loop-Kontext)
        self.in_loop += 1
        self.analyze_block(while_stmt.body)
        self.in_loop -= 1
    
    def analyze_break(self, break_stmt: Break):
        """Analyzes Break"""
        if self.in_loop == 0:
            self.error("Break outside of loop")
    
    def analyze_continue(self, continue_stmt: Continue):
        """Analyzes Continue"""
        if self.in_loop == 0:
            self.error("Continue outside of loop")
    
    # ========================================================================
    # Expression Analysis (mit Type Inference)
    # ========================================================================
    
    def analyze_expression(self, expr: Expression) -> str:
        """
        Analyzes Expression und gibt Typ .
        Annotiert AST mit inferred_type.
        """
        if isinstance(expr, Literal):
            return self.analyze_literal(expr)
        elif isinstance(expr, Identifier):
            return self.analyze_identifier(expr)
        elif isinstance(expr, BinaryOp):
            return self.analyze_binaryop(expr)
        elif isinstance(expr, UnaryOp):
            return self.analyze_unaryop(expr)
        elif isinstance(expr, Call):
            return self.analyze_call(expr)
        elif isinstance(expr, Deref):
            return self.analyze_deref(expr)
        else:
            self.error(f"Unknown expression type: {type(expr).__name__}")
    
    def analyze_literal(self, lit: Literal) -> str:
        """Analyzes Literal"""
        # Integer Literal: Default-Typ = i32
        if lit.type == 'int':
            inferred_type = 'i32'
            lit.inferred_type = inferred_type
            return inferred_type
        
        self.error(f"Unknown literal type: {lit.type}")
    
    def analyze_identifier(self, ident: Identifier) -> str:
        """Analyzes Identifier"""
        symbol = self.lookup_symbol(ident.name)
        
        # AST annotieren
        ident.symbol = symbol
        ident.inferred_type = symbol.type
        
        return symbol.type
    
    def analyze_binaryop(self, binop: BinaryOp) -> str:
        """Analyzes Binary Operation"""
        left_type = self.analyze_expression(binop.left)
        right_type = self.analyze_expression(binop.right)
        
        # Operanden müssen gleichen Typ haben
        if left_type != right_type:
            self.error(f"Type mismatch in binary operation '{binop.op}': {left_type} vs {right_type}")
        
        # Vergleichsoperatoren → bool
        if binop.op in ['==', '!=', '<', '<=', '>', '>=']:
            if not is_integer_type(left_type):
                self.error(f"Comparison operator '{binop.op}' requires integer types, got {left_type}")
            inferred_type = 'bool'
        
        # Arithmetik → gleicher Typ
        elif binop.op in ['+', '-', '*', '/']:
            if not is_integer_type(left_type):
                self.error(f"Arithmetic operator '{binop.op}' requires integer types, got {left_type}")
            inferred_type = left_type
        
        else:
            self.error(f"Unknown binary operator: {binop.op}")
        
        # AST annotieren
        binop.inferred_type = inferred_type
        return inferred_type
    
    def analyze_unaryop(self, unaryop: UnaryOp) -> str:
        """Analyzes Unary Operation"""
        operand_type = self.analyze_expression(unaryop.operand)
        
        if unaryop.op == '-':
            # Negation: nur signed integers
            if operand_type not in SIGNED_TYPES:
                self.error(f"Unary minus requires signed integer, got {operand_type}")
            inferred_type = operand_type
        else:
            self.error(f"Unknown unary operator: {unaryop.op}")
        
        # AST annotieren
        unaryop.inferred_type = inferred_type
        return inferred_type
    
    def analyze_call(self, call: Call) -> str:
        """Analyzes Function Call"""
        func_symbol = self.lookup_function(call.name)
        
        # Argument-Count prüfen
        if len(call.args) != len(func_symbol.params):
            self.error(f"Function '{call.name}' expects {len(func_symbol.params)} arguments, got {len(call.args)}")
        
        # Argument-Typen prüfen
        for i, (arg, (param_name, param_type)) in enumerate(zip(call.args, func_symbol.params)):
            arg_type = self.analyze_expression(arg)
            if arg_type != param_type:
                self.error(f"Argument {i+1} to function '{call.name}': expected {param_type}, got {arg_type}")
        
        # Return-Type
        if not func_symbol.return_type:
            self.error(f"Function '{call.name}' has no return type and cannot be used in expression")
        
        inferred_type = func_symbol.return_type
        
        # AST annotieren
        call.inferred_type = inferred_type
        call.function_symbol = func_symbol
        
        return inferred_type
    
    def analyze_deref(self, deref: Deref) -> str:
        """Analyzes Pointer-Dereferenzierung"""
        operand_type = self.analyze_expression(deref.operand)
        
        if not is_pointer_type(operand_type):
            self.error(f"Cannot dereference non-pointer type: {operand_type}")
        
        # Pointer-Dereferenzierung: Wir kennen den Target-Type nicht
        # for now: Fehler, später mit typed pointers
        self.error("Pointer dereferencing not yet implemented (need typed pointers)")


# ============================================================================
# Utility: Annotated AST Printer
# ============================================================================

def print_annotated_ast(node: ASTNode, indent: int = 0):
    """Gibt annotierten AST aus"""
    prefix = "  " * indent
    
    if isinstance(node, Program):
        print(f"{prefix}Program:")
        for func in node.functions:
            print_annotated_ast(func, indent + 1)
    
    elif isinstance(node, Function):
        params_str = ", ".join(f"{name}: {type_}" for name, type_ in node.params)
        ret_str = f" -> {node.return_type}" if node.return_type else ""
        stack_size = getattr(node, 'stack_size', '?')
        print(f"{prefix}Function: {node.name}({params_str}){ret_str} [stack={stack_size}]")
        print_annotated_ast(node.body, indent + 1)
    
    elif isinstance(node, Block):
        print(f"{prefix}Block:")
        for stmt in node.statements:
            print_annotated_ast(stmt, indent + 1)
    
    elif isinstance(node, VarDecl):
        mut_str = "mut " if node.mutable else ""
        offset = getattr(node, 'stack_offset', '?')
        print(f"{prefix}VarDecl: {mut_str}{node.name}: {node.type} [rbp{offset:+d}]")
        if node.init:
            print_annotated_ast(node.init, indent + 1)
    
    elif isinstance(node, Assignment):
        print(f"{prefix}Assignment:")
        print(f"{prefix}  target:")
        print_annotated_ast(node.target, indent + 2)
        print(f"{prefix}  value:")
        print_annotated_ast(node.value, indent + 2)
    
    elif isinstance(node, Return):
        print(f"{prefix}Return:")
        if node.value:
            print_annotated_ast(node.value, indent + 1)
    
    elif isinstance(node, If):
        print(f"{prefix}If:")
        print(f"{prefix}  condition:")
        print_annotated_ast(node.condition, indent + 2)
        print(f"{prefix}  then:")
        print_annotated_ast(node.then_block, indent + 2)
        if node.else_block:
            print(f"{prefix}  else:")
            print_annotated_ast(node.else_block, indent + 2)
    
    elif isinstance(node, While):
        print(f"{prefix}While:")
        print(f"{prefix}  condition:")
        print_annotated_ast(node.condition, indent + 2)
        print(f"{prefix}  body:")
        print_annotated_ast(node.body, indent + 2)
    
    elif isinstance(node, Break):
        print(f"{prefix}Break")
    
    elif isinstance(node, Continue):
        print(f"{prefix}Continue")
    
    elif isinstance(node, ExprStatement):
        print(f"{prefix}ExprStatement:")
        print_annotated_ast(node.expression, indent + 1)
    
    elif isinstance(node, BinaryOp):
        inferred_type = getattr(node, 'inferred_type', '?')
        print(f"{prefix}BinaryOp: {node.op} → {inferred_type}")
        print_annotated_ast(node.left, indent + 1)
        print_annotated_ast(node.right, indent + 1)
    
    elif isinstance(node, UnaryOp):
        inferred_type = getattr(node, 'inferred_type', '?')
        print(f"{prefix}UnaryOp: {node.op} → {inferred_type}")
        print_annotated_ast(node.operand, indent + 1)
    
    elif isinstance(node, Literal):
        inferred_type = getattr(node, 'inferred_type', '?')
        print(f"{prefix}Literal: {node.value} → {inferred_type}")
    
    elif isinstance(node, Identifier):
        symbol = getattr(node, 'symbol', None)
        inferred_type = getattr(node, 'inferred_type', '?')
        symbol_info = f" [{symbol}]" if symbol else ""
        print(f"{prefix}Identifier: {node.name} → {inferred_type}{symbol_info}")
    
    elif isinstance(node, Call):
        inferred_type = getattr(node, 'inferred_type', '?')
        print(f"{prefix}Call: {node.name}(...) → {inferred_type}")
        for arg in node.args:
            print_annotated_ast(arg, indent + 1)
    
    elif isinstance(node, Deref):
        inferred_type = getattr(node, 'inferred_type', '?')
        print(f"{prefix}Deref: → {inferred_type}")
        print_annotated_ast(node.operand, indent + 1)


# ============================================================================
# Test
# ============================================================================

if __name__ == '__main__':
    from tokenization_engine import Lexer
    
    print("=" * 70)
    print("AXIS Semantic Analyzer - Tests")
    print("=" * 70)
    print()
    
    # Test 1: Simple function
    print("Test 1: Einfache Funktion mit Stack-Layout")
    print("-" * 70)
    source1 = """
    fn main() -> i32 {
        let x: i32 = 10;
        let y: i32 = 20;
        return x;
    }
    """
    
    lexer1 = Lexer(source1)
    tokens1 = lexer1.tokenize()
    parser1 = Parser(tokens1)
    ast1 = parser1.parse()
    
    analyzer1 = SemanticAnalyzer()
    analyzer1.analyze(ast1)
    
    print_annotated_ast(ast1)
    print()
    
    # Test 2: Mit Arithmetik und Type Inference
    print("Test 2: Arithmetik mit Type Inference")
    print("-" * 70)
    source2 = """
    fn add(a: i32, b: i32) -> i32 {
        return a + b;
    }
    
    fn main() -> i32 {
        let x: i32 = 10;
        let y: i32 = 20;
        let z: i32 = add(x, y);
        return z;
    }
    """
    
    lexer2 = Lexer(source2)
    tokens2 = lexer2.tokenize()
    parser2 = Parser(tokens2)
    ast2 = parser2.parse()
    
    analyzer2 = SemanticAnalyzer()
    analyzer2.analyze(ast2)
    
    print_annotated_ast(ast2)
    print()
    
    # Test 3: Control Flow
    print("Test 3: Control Flow")
    print("-" * 70)
    source3 = """
    fn abs(x: i32) -> i32 {
        if x < 0 {
            return -x;
        }
        return x;
    }
    
    fn count() {
        let mut i: i32 = 0;
        while i < 10 {
            i = i + 1;
        }
    }
    """
    
    lexer3 = Lexer(source3)
    tokens3 = lexer3.tokenize()
    parser3 = Parser(tokens3)
    ast3 = parser3.parse()
    
    analyzer3 = SemanticAnalyzer()
    analyzer3.analyze(ast3)
    
    print_annotated_ast(ast3)
    print()
    
    # Test 4: Error tests
    print("Test 4: Fehler-Erkennung")
    print("-" * 70)
    
    error_cases = [
        ("Undefinierte Variable", """
        fn test() -> i32 {
            return x;
        }
        """),
        
        ("Type Mismatch", """
        fn test() -> i32 {
            let x: i64 = 10;
            return x;
        }
        """),
        
        ("Assignment zu Immutable", """
        fn test() {
            let x: i32 = 10;
            x = 20;
        }
        """),
        
        ("Break außerhalb Loop", """
        fn test() {
            break;
        }
        """),
    ]
    
    for name, source in error_cases:
        try:
            lexer = Lexer(source)
            tokens = lexer.tokenize()
            parser = Parser(tokens)
            ast = parser.parse()
            analyzer = SemanticAnalyzer()
            analyzer.analyze(ast)
            print(f"  ✗ {name}: FEHLER - Sollte Exception werfen!")
        except SemanticError as e:
            print(f"  ✓ {name}: {e}")
        except Exception as e:
            print(f"  ? {name}: Unerwartete Exception: {e}")
    
    print()
    print("=" * 70)
    print("Alle Tests abgeschlossen")
    print("=" * 70)
