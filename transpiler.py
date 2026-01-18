"""
AXIS to Python Transpiler
Converts AXIS AST to Python source code, then executes it.
This leverages Python's own optimized bytecode compiler for maximum speed.
"""

from typing import Dict, List
from syntactic_analyzer import (
    Program, Function, Statement, VarDecl, Assignment, Return,
    If, While, Break, Continue, Write, BinaryOp, UnaryOp,
    Literal, StringLiteral, Identifier, Call, ExprStatement,
    Read, Readln, Readchar, ReadFailed
)


class PythonTranspiler:
    """Transpiles AXIS AST to Python source code"""
    
    def __init__(self):
        self.indent = 0
        self.lines: List[str] = []
        self.functions: Dict[str, Function] = {}
    
    def _emit(self, line: str):
        """Emit a line of Python code with proper indentation"""
        self.lines.append("    " * self.indent + line)
    
    def transpile(self, program: Program) -> str:
        """Transpile a program to Python source code"""
        self.lines = []
        
        # Emit runtime support
        self._emit("import sys as _sys")
        self._emit("_read_failed = False")
        self._emit("")
        
        # Transpile functions first
        for func in program.functions:
            self._transpile_function(func)
        
        # Transpile top-level statements
        for stmt in program.statements:
            self._transpile_stmt(stmt)
        
        return "\n".join(self.lines)
    
    def _transpile_function(self, func: Function):
        """Transpile a function definition"""
        self._emit(f"def {func.name}():")
        self.indent += 1
        if not func.body.statements:
            self._emit("pass")
        else:
            for stmt in func.body.statements:
                self._transpile_stmt(stmt)
        self.indent -= 1
        self._emit("")
    
    def _transpile_stmt(self, stmt: Statement):
        """Transpile a statement"""
        t = type(stmt)
        
        if t is VarDecl:
            if stmt.init:
                self._emit(f"{stmt.name} = {self._transpile_expr(stmt.init)}")
            else:
                self._emit(f"{stmt.name} = None")
        
        elif t is Assignment:
            name = stmt.target.name
            self._emit(f"{name} = {self._transpile_expr(stmt.value)}")
        
        elif t is Return:
            if stmt.value:
                self._emit(f"return {self._transpile_expr(stmt.value)}")
            else:
                self._emit("return None")
        
        elif t is If:
            self._emit(f"if {self._transpile_expr(stmt.condition)}:")
            self.indent += 1
            if not stmt.then_block.statements:
                self._emit("pass")
            else:
                for s in stmt.then_block.statements:
                    self._transpile_stmt(s)
            self.indent -= 1
            
            if stmt.else_block:
                self._emit("else:")
                self.indent += 1
                if not stmt.else_block.statements:
                    self._emit("pass")
                else:
                    for s in stmt.else_block.statements:
                        self._transpile_stmt(s)
                self.indent -= 1
        
        elif t is While:
            self._emit(f"while {self._transpile_expr(stmt.condition)}:")
            self.indent += 1
            if not stmt.body.statements:
                self._emit("pass")
            else:
                for s in stmt.body.statements:
                    self._transpile_stmt(s)
            self.indent -= 1
        
        elif t is Break:
            self._emit("break")
        
        elif t is Continue:
            self._emit("continue")
        
        elif t is Write:
            expr = self._transpile_expr(stmt.value)
            if stmt.newline:
                self._emit(f"print({expr})")
            else:
                self._emit(f"print({expr}, end='')")
        
        elif t is ExprStatement:
            # Expression statement (e.g., function call)
            self._emit(self._transpile_expr(stmt.expression))
        
        else:
            # Unknown statement - try as expression
            self._emit(self._transpile_expr(stmt))
    
    def _transpile_expr(self, expr) -> str:
        """Transpile an expression to Python code"""
        t = type(expr)
        
        if t is Literal:
            if expr.type == 'bool':
                return "True" if (expr.value == 'True' or expr.value == '1') else "False"
            return expr.value
        
        if t is StringLiteral:
            # Escape and quote string
            escaped = expr.value.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\r', '\\r')
            return f'"{escaped}"'
        
        if t is Identifier:
            return expr.name
        
        if t is BinaryOp:
            left = self._transpile_expr(expr.left)
            right = self._transpile_expr(expr.right)
            op = expr.op
            
            # Map AXIS operators to Python
            if op == '/':
                return f"({left} // {right})"  # Integer division
            elif op == '!':
                return f"(not {right})"
            else:
                return f"({left} {op} {right})"
        
        if t is UnaryOp:
            operand = self._transpile_expr(expr.operand)
            if expr.op == '!':
                return f"(not {operand})"
            return f"({expr.op}{operand})"
        
        if t is Call:
            return f"{expr.name}()"
        
        if t is Read:
            return "_sys.stdin.read()"
        
        if t is Readln:
            return "input()"
        
        if t is Readchar:
            return "(lambda c: ord(c) if c else -1)(_sys.stdin.read(1))"
        
        if t is ReadFailed:
            return "_read_failed"
        
        raise RuntimeError(f"Unknown expression: {t.__name__}")


# =============================================================================
# Main Entry Points
# =============================================================================

def run_transpiled(program: 'Program', verbose: bool = False) -> int:
    """Transpile and execute a program"""
    transpiler = PythonTranspiler()
    python_code = transpiler.transpile(program)
    
    if verbose:
        print("=== Generated Python Code ===")
        for i, line in enumerate(python_code.split('\n'), 1):
            print(f"{i:4}: {line}")
        print("=== End Python Code ===\n")
    
    # Execute the transpiled code
    namespace = {}
    exec(python_code, namespace)
    
    return 0


def run_script(source: str, source_path: str = None, verbose: bool = False) -> int:
    """Full pipeline: lex -> parse -> transpile -> execute"""
    from tokenization_engine import Lexer
    from syntactic_analyzer import Parser
    
    lexer = Lexer(source)
    tokens = lexer.tokenize()
    parser = Parser(tokens)
    program = parser.parse()
    
    if program.mode != "script":
        raise RuntimeError("Source is not in script mode")
    
    return run_transpiled(program, verbose)


if __name__ == '__main__':
    test_code = '''mode script

writeln("Transpiler Test")
x: i32 = 0
repeat:
    x = x + 1
    when x >= 10:
        stop
writeln(x)
'''
    
    run_script(test_code, "test.axis", verbose=True)
