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
    Read, Readln, Readchar, ReadFailed, ArrayLiteral, IndexAccess,
    CopyExpr, FieldDef, FieldMember, FieldAccess,
    EnumDef, EnumVariant, EnumAccess, Match, MatchArm
)


class PythonTranspiler:
    """Transpiles AXIS AST to Python source code"""
    
    def __init__(self):
        self.indent = 0
        self.lines: List[str] = []
        self.functions: Dict[str, Function] = {}
        self.field_defs: Dict[str, FieldDef] = {}  # Field type definitions
        self.update_params: List[str] = []  # Current function's update params
        self.current_function: Function = None  # Current function being transpiled
    
    def _emit(self, line: str):
        """Emit a line of Python code with proper indentation"""
        self.lines.append("    " * self.indent + line)
    
    def transpile(self, program: Program) -> str:
        """Transpile a program to Python source code"""
        self.lines = []
        
        # Emit runtime support
        self._emit("import sys as _sys")
        self._emit("from copy import deepcopy as _deepcopy")
        self._emit("_read_failed = False")
        self._emit("")
        
        # Pointer runtime support removed in v1.1.0 - AXIS uses value semantics
        
        # Store field definitions
        for field_def in program.field_defs:
            self.field_defs[field_def.name] = field_def
        
        # Transpile field types as Python classes
        for field_def in program.field_defs:
            self._transpile_field_def(field_def)
        
        # Transpile enum types as Python classes
        for enum_def in program.enum_defs:
            self._transpile_enum_def(enum_def)
        
        # Store function info for call-site handling of update params
        for func in program.functions:
            self.functions[func.name] = func
        
        # Transpile functions first
        for func in program.functions:
            self._transpile_function(func)
        
        # Transpile top-level statements
        for stmt in program.statements:
            self._transpile_stmt(stmt)
        
        return "\n".join(self.lines)
    
    def _transpile_field_def(self, field_def: FieldDef, class_name: str = None):
        """Transpile a field definition to a Python class"""
        name = class_name or field_def.name
        self._emit(f"class {name}:")
        self.indent += 1
        self._emit("def __init__(self):")
        self.indent += 1
        
        if not field_def.members:
            self._emit("pass")
        else:
            for member in field_def.members:
                self._transpile_field_member_init(member)
        
        self.indent -= 1
        self.indent -= 1
        self._emit("")
    
    def _transpile_enum_def(self, enum_def: EnumDef):
        """Transpile an enum definition to a Python class with class attributes"""
        self._emit(f"class {enum_def.name}:")
        self.indent += 1
        
        if not enum_def.variants:
            self._emit("pass")
        else:
            for variant in enum_def.variants:
                self._emit(f"{variant.name} = {variant.value}")
        
        self.indent -= 1
        self._emit("")
    
    def _transpile_field_member_init(self, member: FieldMember, prefix: str = "self"):
        """Emit initialization for a field member"""
        if member.type == 'field':
            # Inline nested field - create nested object
            if member.inline_field:
                # Create an anonymous class for this inline field
                anon_class = f"_{member.name}_field"
                # We need to emit the nested class, but for simplicity
                # we'll create a simple object with attributes
                self._emit(f"{prefix}.{member.name} = type('{anon_class}', (), {{}})()")
                # Initialize nested members
                for nested_member in member.inline_field.members:
                    self._transpile_field_member_init(nested_member, f"{prefix}.{member.name}")
        elif member.type == 'array':
            if member.array_type:
                size = member.array_type.size or 0
                elem_type = member.array_type.element_type
                if elem_type == 'field' and member.inline_field:
                    # Array of inline fields
                    anon_class = f"_{member.name}_elem"
                    # Create a list of anonymous objects
                    self._emit(f"{prefix}.{member.name} = []")
                    self._emit(f"for _i in range({size}):")
                    self.indent += 1
                    self._emit(f"_elem = type('{anon_class}', (), {{}})()")
                    for nested_member in member.inline_field.members:
                        self._transpile_field_member_init(nested_member, "_elem")
                    self._emit(f"{prefix}.{member.name}.append(_elem)")
                    self.indent -= 1
                elif elem_type in self.field_defs:
                    # Array of named field types
                    self._emit(f"{prefix}.{member.name} = [{elem_type}() for _i in range({size})]")
                else:
                    # Array of scalar types
                    default_val = self._get_default_value(elem_type)
                    self._emit(f"{prefix}.{member.name} = [{default_val}] * {size}")
        elif member.type in self.field_defs:
            # Named field type member
            self._emit(f"{prefix}.{member.name} = {member.type}()")
        else:
            # Scalar type
            if member.default_value:
                default = self._transpile_expr(member.default_value)
            else:
                default = self._get_default_value(member.type)
            self._emit(f"{prefix}.{member.name} = {default}")
    
    def _get_default_value(self, type_name: str) -> str:
        """Get default value for a type"""
        if type_name in ('i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64'):
            return "0"
        elif type_name == 'bool':
            return "False"
        elif type_name == 'str':
            return '""'
        else:
            return "None"
    
    def _transpile_function(self, func: Function):
        """Transpile a function definition"""
        self.current_function = func  # Track for update params handling
        
        # Build parameter list from Parameter objects
        params = ", ".join(p.name for p in func.params)
        self._emit(f"def {func.name}({params}):")
        self.indent += 1
        
        # Track which params are 'update' for return handling
        self.update_params = [p.name for p in func.params if p.modifier == 'update']
        
        if not func.body.statements:
            if self.update_params:
                # Return update params even if body is empty
                if len(self.update_params) == 1:
                    self._emit(f"return {self.update_params[0]}")
                else:
                    self._emit(f"return ({', '.join(self.update_params)},)")
            else:
                self._emit("pass")
        else:
            for stmt in func.body.statements:
                self._transpile_stmt(stmt)
            
            # Add implicit return of update params if no explicit return at end
            if self.update_params:
                last_stmt = func.body.statements[-1] if func.body.statements else None
                if not isinstance(last_stmt, Return):
                    if len(self.update_params) == 1:
                        self._emit(f"return {self.update_params[0]}")
                    else:
                        self._emit(f"return ({', '.join(self.update_params)},)")
        
        self.update_params = []
        self.current_function = None
        self.indent -= 1
        self._emit("")
    
    def _transpile_stmt(self, stmt: Statement):
        """Transpile a statement"""
        t = type(stmt)
        
        if t is VarDecl:
            if stmt.init:
                # Check if init is a call to function with update params
                if type(stmt.init) is Call:
                    call = stmt.init
                    if call.name in self.functions:
                        func = self.functions[call.name]
                        update_param_indices = []
                        for i, p in enumerate(func.params):
                            if p.modifier == 'update':
                                update_param_indices.append(i)
                        
                        if update_param_indices:
                            # Need to unpack return and assign back to update params
                            args_exprs = [self._transpile_expr(arg) for arg in call.args]
                            
                            # Get the variable names for update params at call site
                            update_vars = []
                            for idx in update_param_indices:
                                if idx < len(call.args) and type(call.args[idx]) is Identifier:
                                    update_vars.append(call.args[idx].name)
                                else:
                                    update_vars.append(None)
                            
                            # Call the function
                            result_var = "_result"
                            args = ", ".join(args_exprs)
                            self._emit(f"{result_var} = {call.name}({args})")
                            
                            # Assign return value to declared variable
                            # Assign back update params
                            if func.return_type and func.return_type != 'void':
                                # Has return value - tuple of (ret, updates...)
                                self._emit(f"{stmt.name} = {result_var}[0]")
                                for i, var in enumerate(update_vars):
                                    if var:
                                        self._emit(f"{var} = {result_var}[{i + 1}]")
                            else:
                                # No return value - use None for declaration
                                self._emit(f"{stmt.name} = None")
                                if len(update_vars) == 1 and update_vars[0]:
                                    self._emit(f"{update_vars[0]} = {result_var}")
                                else:
                                    for i, var in enumerate(update_vars):
                                        if var:
                                            self._emit(f"{var} = {result_var}[{i}]")
                            return
                
                # Regular initialization
                self._emit(f"{stmt.name} = {self._transpile_expr(stmt.init)}")
            else:
                # No initializer - check what type of variable this is
                if stmt.type in self.field_defs:
                    # Named field type
                    self._emit(f"{stmt.name} = {stmt.type}()")
                elif stmt.type == 'array' and stmt.array_type:
                    # Array type - check if element is a field type
                    elem_type = stmt.array_type.element_type
                    size = stmt.array_type.size or 0
                    if elem_type in self.field_defs:
                        # Array of named field types
                        self._emit(f"{stmt.name} = [{elem_type}() for _i in range({size})]")
                    else:
                        # Array of scalar types
                        default_val = self._get_default_value(elem_type)
                        self._emit(f"{stmt.name} = [{default_val}] * {size}")
                else:
                    self._emit(f"{stmt.name} = None")
        
        elif t is Assignment:
            # Handle simple assignments and array index assignments
            if type(stmt.target) is Identifier:
                name = stmt.target.name
                self._emit(f"{name} = {self._transpile_expr(stmt.value)}")
            elif type(stmt.target) is IndexAccess:
                array = self._transpile_expr(stmt.target.array)
                index = self._transpile_expr(stmt.target.index)
                self._emit(f"{array}[{index}] = {self._transpile_expr(stmt.value)}")
            elif type(stmt.target) is FieldAccess:
                # Field member assignment: obj.field = value
                target = self._transpile_expr(stmt.target)
                self._emit(f"{target} = {self._transpile_expr(stmt.value)}")
            # Pointer dereference removed in v1.1.0 - AXIS uses value semantics
        
        elif t is Return:
            # Handle update params - they need to be returned as part of a tuple
            if self.update_params:
                if stmt.value:
                    return_val = self._transpile_expr(stmt.value)
                    all_returns = [return_val] + self.update_params
                    self._emit(f"return ({', '.join(all_returns)},)")
                else:
                    # No return value, just return update params
                    if len(self.update_params) == 1:
                        self._emit(f"return {self.update_params[0]}")
                    else:
                        self._emit(f"return ({', '.join(self.update_params)},)")
            else:
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
        
        elif t is Match:
            # Transpile match statement to Python if-elif-else chain
            value_expr = self._transpile_expr(stmt.value)
            match_var = "_match_val"
            self._emit(f"{match_var} = {value_expr}")
            
            first_arm = True
            wildcard_arm = None
            
            for arm in stmt.arms:
                if arm.is_wildcard:
                    wildcard_arm = arm
                    continue
                
                pattern_expr = self._transpile_expr(arm.pattern)
                
                if first_arm:
                    self._emit(f"if {match_var} == {pattern_expr}:")
                    first_arm = False
                else:
                    self._emit(f"elif {match_var} == {pattern_expr}:")
                
                self.indent += 1
                if not arm.body.statements:
                    self._emit("pass")
                else:
                    for s in arm.body.statements:
                        self._transpile_stmt(s)
                self.indent -= 1
            
            # Handle wildcard/default case
            if wildcard_arm:
                self._emit("else:")
                self.indent += 1
                if not wildcard_arm.body.statements:
                    self._emit("pass")
                else:
                    for s in wildcard_arm.body.statements:
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
            # Check if it's a call to a function with update params
            if type(stmt.expression) is Call:
                call = stmt.expression
                if call.name in self.functions:
                    func = self.functions[call.name]
                    update_param_indices = []
                    for i, p in enumerate(func.params):
                        if p.modifier == 'update':
                            update_param_indices.append(i)
                    
                    if update_param_indices:
                        # Need to unpack return and assign back to update params
                        args_exprs = [self._transpile_expr(arg) for arg in call.args]
                        
                        # Get the variable names for update params at call site
                        # The arguments at update positions should be identifiers
                        update_vars = []
                        for idx in update_param_indices:
                            if idx < len(call.args) and type(call.args[idx]) is Identifier:
                                update_vars.append(call.args[idx].name)
                            else:
                                update_vars.append(None)
                        
                        # Call the function and unpack
                        result_var = "_result"
                        args = ", ".join(args_exprs)
                        self._emit(f"{result_var} = {call.name}({args})")
                        
                        # Assign back update params
                        # Return format: (return_val, update1, update2, ...) or just update1 if no return
                        if func.return_type and func.return_type != 'void':
                            # Has return value - tuple of (ret, updates...)
                            for i, var in enumerate(update_vars):
                                if var:
                                    self._emit(f"{var} = {result_var}[{i + 1}]")
                        else:
                            # No return value - just update params
                            if len(update_vars) == 1 and update_vars[0]:
                                self._emit(f"{update_vars[0]} = {result_var}")
                            else:
                                for i, var in enumerate(update_vars):
                                    if var:
                                        self._emit(f"{var} = {result_var}[{i}]")
                        return
            
            # Regular expression statement
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
            args = ", ".join(self._transpile_expr(arg) for arg in expr.args)
            return f"{expr.name}({args})"
        
        if t is Read:
            return "_sys.stdin.read()"
        
        if t is Readln:
            return "input()"
        
        if t is Readchar:
            return "(lambda c: ord(c) if c else -1)(_sys.stdin.read(1))"
        
        if t is ReadFailed:
            return "_read_failed"
        
        if t is ArrayLiteral:
            elements = ", ".join(self._transpile_expr(e) for e in expr.elements)
            return f"[{elements}]"
        
        if t is IndexAccess:
            array = self._transpile_expr(expr.array)
            index = self._transpile_expr(expr.index)
            return f"{array}[{index}]"
        
        if t is CopyExpr:
            # For arrays, use list(); for fields use deepcopy
            # In script mode, copy.runtime and copy.compile behave identically
            operand = self._transpile_expr(expr.operand)
            # We use deepcopy to handle both cases properly
            return f"_deepcopy({operand})"
        
        if t is FieldAccess:
            # Field member access: obj.member
            obj = self._transpile_expr(expr.object)
            return f"{obj}.{expr.member}"
        
        if t is EnumAccess:
            # Enum variant access: EnumName.VariantName
            return f"{expr.enum_name}.{expr.variant_name}"
        
        # Pointer expression types removed in v1.1.0 - AXIS uses value semantics
        
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
    """Full pipeline: lex -> parse -> analyze -> transpile -> execute"""
    from tokenization_engine import Lexer
    from syntactic_analyzer import Parser
    from semantic_analyzer import SemanticAnalyzer
    
    lexer = Lexer(source)
    tokens = lexer.tokenize()
    parser = Parser(tokens)
    program = parser.parse()
    
    if program.mode != "script":
        raise RuntimeError("Source is not in script mode")
    
    # Run semantic analysis for type checking and 'copy' enforcement
    analyzer = SemanticAnalyzer()
    analyzer.analyze(program)
    
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
