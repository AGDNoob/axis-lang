"""AXIS Semantic Analyzer - type checking und symbol table kram"""

from dataclasses import dataclass, field
from typing import Optional, Dict
from syntactic_analyzer import *


TYPE_SIZES = {
    'i8': 1, 'i16': 2, 'i32': 4, 'i64': 8,
    'u8': 1, 'u16': 2, 'u32': 4, 'u64': 8,
    'bool': 1,
    'str': 8,  # pointer to string data
}

SIGNED_TYPES = {'i8', 'i16', 'i32', 'i64'}
UNSIGNED_TYPES = {'u8', 'u16', 'u32', 'u64'}
INTEGER_TYPES = SIGNED_TYPES | UNSIGNED_TYPES
SCALAR_TYPES = INTEGER_TYPES | {'bool', 'str'}
VALID_TYPES = SCALAR_TYPES | {'array', 'field'}


def is_integer_type(type_name: str) -> bool:
    return type_name in INTEGER_TYPES


# Pointer functions removed in v1.1.0 - AXIS uses value semantics


def get_type_size(type_name: str) -> int:
    if type_name not in TYPE_SIZES:
        raise SemanticError(f"Unknown type: {type_name}")
    return TYPE_SIZES[type_name]


def align_offset(offset: int, alignment: int) -> int:
    return ((offset + alignment - 1) // alignment) * alignment


@dataclass
class Symbol:
    """ein symbol in der symbol table, nix besonderes"""
    name: str
    type: str
    mutable: bool
    stack_offset: int = 0
    is_param: bool = False
    param_modifier: Optional[str] = None  # 'update', 'copy', or None for regular params
    array_type: 'ArrayType' = None  # For array variables
    
    def __repr__(self):
        mut = "mut " if self.mutable else ""
        param = " [param]" if self.is_param else ""
        mod = f" [{self.param_modifier}]" if self.param_modifier else ""
        arr = f" [{self.array_type}]" if self.array_type else ""
        return f"Symbol({mut}{self.name}: {self.type}{arr} @ rbp{self.stack_offset:+d}{param}{mod})"


@dataclass
class FunctionSymbol:
    name: str
    params: list['Parameter']  # Now using Parameter objects with modifiers
    return_type: Optional[str]
    
    def __repr__(self):
        params_str = ", ".join(
            f"{p.modifier + ' ' if p.modifier else ''}{p.name}: {p.type}" 
            for p in self.params
        )
        ret_str = f" -> {self.return_type}" if self.return_type else ""
        return f"Function({self.name}({params_str}){ret_str})"


class Scope:
    def __init__(self, parent: Optional['Scope'] = None):
        self.parent = parent
        self.symbols: Dict[str, Symbol] = {}
    
    def define(self, symbol: Symbol):
        if symbol.name in self.symbols:
            raise SemanticError(f"Symbol '{symbol.name}' already defined in this scope")
        self.symbols[symbol.name] = symbol
    
    def lookup(self, name: str) -> Optional[Symbol]:
        # lookup in current scope, dann parent scopes
        if name in self.symbols:
            return self.symbols[name]
        if self.parent:
            return self.parent.lookup(name)
        return None
    
    def lookup_local(self, name: str) -> Optional[Symbol]:
        return self.symbols.get(name)


class SemanticError(Exception):
    pass


class SemanticAnalyzer:
    def __init__(self):
        self.global_scope: Optional[Scope] = None
        self.current_scope: Optional[Scope] = None
        self.functions: Dict[str, FunctionSymbol] = {}
        self.field_types: Dict[str, 'FieldDef'] = {}  # Field type definitions
        self.enum_types: Dict[str, 'EnumDef'] = {}    # Enum type definitions
        self.current_function: Optional[Function] = None
        self.current_stack_offset: int = 0
        self.in_loop: int = 0
    
    def error(self, msg: str):
        raise SemanticError(msg)
    
    def enter_scope(self):
        self.current_scope = Scope(parent=self.current_scope)
    
    def exit_scope(self):
        if not self.current_scope:
            self.error("Cannot exit scope: no current scope")
        self.current_scope = self.current_scope.parent
    
    def define_symbol(self, name: str, type_: str, mutable: bool, is_param: bool = False, 
                       param_modifier: Optional[str] = None, array_type: 'ArrayType' = None) -> Symbol:
        if not self.current_scope:
            self.error("No current scope")
        
        # Prüfe Typ-Validität - scalar types, array, field, enum, or named types
        if type_ not in VALID_TYPES and type_ not in self.field_types and type_ not in self.enum_types:
            self.error(f"Unknown type: {type_}")
        
        # Get type size - field types have dynamic size, enum types use their underlying type
        if type_ in self.field_types:
            type_size = self.get_field_size(type_)
        elif type_ in self.enum_types:
            # Use the enum's underlying type size
            enum_def = self.enum_types[type_]
            type_size = TYPE_SIZES.get(enum_def.underlying_type, 4)
        elif type_ in TYPE_SIZES:
            type_size = TYPE_SIZES[type_]
        else:
            type_size = 8  # default for arrays, etc.
        
        if is_param:
            stack_offset = 0
        else:
            alignment = min(type_size, 8)
            self.current_stack_offset = align_offset(self.current_stack_offset, alignment)
            self.current_stack_offset += type_size
            stack_offset = -self.current_stack_offset
        
        symbol = Symbol(name, type_, mutable, stack_offset, is_param, param_modifier, array_type)
        self.current_scope.define(symbol)
        return symbol
    
    def lookup_symbol(self, name: str) -> Symbol:
        if not self.current_scope:
            self.error("No current scope")
        
        symbol = self.current_scope.lookup(name)
        if not symbol:
            self.error(f"Undefined variable: {name}")
        return symbol
    
    def lookup_function(self, name: str) -> FunctionSymbol:
        if name not in self.functions:
            self.error(f"Undefined function: {name}")
        return self.functions[name]
    
    def analyze(self, program: Program):
        # Global Scope erstellen
        self.global_scope = Scope()
        self.current_scope = self.global_scope
        
        # Pass 0a: Collect all field type definitions
        for field_def in program.field_defs:
            if field_def.name in self.field_types:
                self.error(f"Duplicate field type definition: {field_def.name}")
            self.field_types[field_def.name] = field_def
        
        # Pass 0b: Collect all enum type definitions
        for enum_def in program.enum_defs:
            if enum_def.name in self.enum_types:
                self.error(f"Duplicate enum type definition: {enum_def.name}")
            if enum_def.name in self.field_types:
                self.error(f"Enum name conflicts with field type: {enum_def.name}")
            self.enum_types[enum_def.name] = enum_def
        
        # Validate all field definitions (check for undefined nested types, etc.)
        for field_def in program.field_defs:
            self.analyze_field_def(field_def)
        
        # Pass 1: Sammle alle Funktions-Signaturen
        for func in program.functions:
            if func.name in self.functions:
                self.error(f"Duplicate function definition: {func.name}")
            
            func_symbol = FunctionSymbol(func.name, func.params, func.return_type)
            self.functions[func.name] = func_symbol
        
        # Pass 2: Analysiere Funktions-Bodies
        for func in program.functions:
            self.analyze_function(func)
        
        # Pass 3: Analyze top-level statements (script mode)
        if program.statements:
            self.current_function = None
            self.current_stack_offset = 0
            for stmt in program.statements:
                self.analyze_statement(stmt)
    
    def analyze_field_def(self, field_def: 'FieldDef'):
        """Validate a field definition"""
        for member in field_def.members:
            self.analyze_field_member(member)
    
    def analyze_field_member(self, member: 'FieldMember'):
        """Validate a field member"""
        if member.type == 'field':
            # Inline nested field - recursively validate
            if member.inline_field:
                self.analyze_field_def(member.inline_field)
        elif member.type == 'array':
            # Array member - validate element type
            if member.array_type:
                elem_type = member.array_type.element_type
                if elem_type == 'field':
                    # Array of inline fields
                    if member.inline_field:
                        self.analyze_field_def(member.inline_field)
                elif elem_type not in SCALAR_TYPES and elem_type not in self.field_types:
                    self.error(f"Unknown array element type in field member '{member.name}': {elem_type}")
        elif member.type not in SCALAR_TYPES and member.type not in self.field_types:
            self.error(f"Unknown type in field member '{member.name}': {member.type}")
        
        # Validate default value if present
        if member.default_value:
            # TODO: Type check default value expression
            pass
    
    def get_field_size(self, field_type_name: str) -> int:
        """Calculate the total size of a field type"""
        if field_type_name not in self.field_types:
            self.error(f"Unknown field type: {field_type_name}")
        
        field_def = self.field_types[field_type_name]
        return self.calculate_field_size(field_def)
    
    def calculate_field_size(self, field_def: 'FieldDef') -> int:
        """Calculate size of a field definition (including inline fields)"""
        total_size = 0
        for member in field_def.members:
            if member.type == 'field':
                if member.inline_field:
                    total_size += self.calculate_field_size(member.inline_field)
            elif member.type == 'array':
                if member.array_type:
                    elem_type = member.array_type.element_type
                    if elem_type == 'field' and member.inline_field:
                        elem_size = self.calculate_field_size(member.inline_field)
                    elif elem_type in self.field_types:
                        elem_size = self.get_field_size(elem_type)
                    elif elem_type in TYPE_SIZES:
                        elem_size = TYPE_SIZES[elem_type]
                    else:
                        elem_size = 8
                    total_size += elem_size * (member.array_type.size or 1)
            elif member.type in self.field_types:
                total_size += self.get_field_size(member.type)
            elif member.type in TYPE_SIZES:
                total_size += TYPE_SIZES[member.type]
            else:
                total_size += 8  # default
        return total_size
    
    def analyze_function(self, func: Function):
        self.current_function = func
        self.current_stack_offset = 0
        
        # Neuer Scope für Funktion
        self.enter_scope()
        
        # Track update params for pointer storage
        func.update_param_slots = {}  # param_name -> pointer_slot_offset
        
        # Register parameters with modifiers
        # Parameters are stored on stack for easy access
        for param in func.params:
            # 'update' params need to be mutable (they will be written back)
            # 'copy' params create a local copy, also mutable
            # Regular params are immutable by default
            is_mutable = param.modifier in ('update', 'copy')
            
            # Allocate stack space for parameter
            param_type = param.type
            
            # Get type size - handle field types
            if param_type in self.field_types:
                type_size = self.get_field_size(param_type)
            elif param_type == 'array':
                type_size = 8  # arrays passed as ptr
            elif param_type in TYPE_SIZES:
                type_size = TYPE_SIZES[param_type]
            else:
                self.error(f"Unknown parameter type: {param_type}")
            
            alignment = min(type_size, 8)
            self.current_stack_offset = align_offset(self.current_stack_offset, alignment)
            self.current_stack_offset += type_size
            stack_offset = -self.current_stack_offset
            
            symbol = Symbol(
                param.name, 
                param_type, 
                is_mutable, 
                stack_offset,
                is_param=True,
                param_modifier=param.modifier,
                array_type=param.array_type
            )
            self.current_scope.define(symbol)
            
            # Store param index for code generator
            param.stack_offset = stack_offset
            
            # For 'update' params, allocate extra slot to store the caller's address
            if param.modifier == 'update':
                self.current_stack_offset = align_offset(self.current_stack_offset, 8)
                self.current_stack_offset += 8  # pointer size
                param.ptr_slot_offset = -self.current_stack_offset
                func.update_param_slots[param.name] = param.ptr_slot_offset
        
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
        # Neuer Scope für Block
        self.enter_scope()
        
        for stmt in block.statements:
            self.analyze_statement(stmt)
        
        self.exit_scope()
    
    def analyze_statement(self, stmt: Statement):
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
        elif isinstance(stmt, Write):
            self.analyze_write(stmt)
        elif isinstance(stmt, Match):
            self.analyze_match(stmt)
        elif isinstance(stmt, ExprStatement):
            self.analyze_expression(stmt.expression)
        else:
            self.error(f"Unknown statement type: {type(stmt).__name__}")
    
    def analyze_vardecl(self, vardecl: VarDecl):
        # Handle array declarations
        if vardecl.type == 'array' and vardecl.array_type:
            self.analyze_array_vardecl(vardecl)
            return
        
        # Typ validieren (scalar types, field types, or enum types)
        if vardecl.type not in VALID_TYPES and vardecl.type not in self.field_types and vardecl.type not in self.enum_types:
            self.error(f"Unknown type: {vardecl.type}")
        
        # Check if this is a field type variable
        if vardecl.type in self.field_types:
            # Field type variable - no init expression required
            # (fields use default values from their definition)
            symbol = self.define_symbol(vardecl.name, vardecl.type, vardecl.mutable)
            vardecl.stack_offset = symbol.stack_offset
            vardecl.symbol = symbol
            vardecl.is_field = True
            return
        
        # Check if this is an enum type variable
        if vardecl.type in self.enum_types:
            # Enum type variable - validate initialization is of correct enum type
            if vardecl.init:
                init_type = self.analyze_expression(vardecl.init)
                if init_type != vardecl.type:
                    self.error(f"Type mismatch in variable '{vardecl.name}': expected {vardecl.type}, got {init_type}")
            
            # Enums are stored as i32 values
            symbol = self.define_symbol(vardecl.name, vardecl.type, vardecl.mutable)
            vardecl.stack_offset = symbol.stack_offset
            vardecl.symbol = symbol
            vardecl.is_enum = True
            return
        
        # Init-Expression typisieren (If present)
        if vardecl.init:
            # Special handling for read expressions - pass target type
            if isinstance(vardecl.init, Read):
                init_type = self.analyze_read(vardecl.init, vardecl.type)
            elif isinstance(vardecl.init, Readln):
                init_type = self.analyze_readln(vardecl.init, vardecl.type)
            elif isinstance(vardecl.init, Readchar):
                init_type = self.analyze_readchar(vardecl.init, vardecl.type)
            else:
                init_type = self.analyze_expression(vardecl.init)
            
            # Type-Check: init muss vom gleichen Typ sein
            # Special case: Allow i32 literals to be assigned to other integer types if in range
            if init_type == 'i32' and vardecl.type in ['i8', 'i16', 'i64', 'u8', 'u16', 'u32', 'u64']:
                if isinstance(vardecl.init, Literal):
                    # Allow literal coercion
                    vardecl.init.inferred_type = vardecl.type
                    init_type = vardecl.type
                else:
                    self.error(f"Type mismatch in variable '{vardecl.name}': expected {vardecl.type}, got {init_type}")
            # Special case: Allow i32 literals 0/1 to be assigned to bool
            elif vardecl.type == 'bool' and init_type == 'i32':
                # Check if it's a literal 0 or 1
                if isinstance(vardecl.init, Literal) and vardecl.init.value in ['0', '1']:
                    # Convert literal type to bool
                    vardecl.init.inferred_type = 'bool'
                    init_type = 'bool'
                else:
                    self.error(f"Cannot assign non-boolean value to bool variable '{vardecl.name}'")
            # Pointer type removed in v1.1.0 - AXIS uses value semantics
            elif init_type != vardecl.type:
                self.error(f"Type mismatch in variable '{vardecl.name}': expected {vardecl.type}, got {init_type}")
        
        # Symbol definieren
        symbol = self.define_symbol(vardecl.name, vardecl.type, vardecl.mutable)
        
        # AST annotieren
        vardecl.stack_offset = symbol.stack_offset
        vardecl.symbol = symbol
    
    def analyze_array_vardecl(self, vardecl: VarDecl):
        """Analyze array variable declaration: arr: (i32; 5) = [1, 2, 3, 4, 5]"""
        array_type = vardecl.array_type
        element_type = array_type.element_type
        
        # Validate element type - can be scalar or field type
        if element_type not in VALID_TYPES and element_type not in self.field_types:
            self.error(f"Unknown array element type: {element_type}")
        
        # Check if element is a field type
        is_field_element = element_type in self.field_types
        
        # Check init expression
        if vardecl.init:
            if is_field_element:
                self.error(f"Arrays of field types cannot have initializers - use index assignment instead")
            
            # Allow array literals or copy expressions
            if isinstance(vardecl.init, CopyExpr):
                # CopyExpr is valid for arrays - analyze the operand
                self.analyze_copy_expr(vardecl.init)
            elif isinstance(vardecl.init, ArrayLiteral):
                # Analyze the array literal with expected element type
                self.analyze_array_literal(vardecl.init, element_type)
                
                # Get the actual size from the literal
                actual_size = len(vardecl.init.elements)
                
                # In compile mode, size must match if specified
                if array_type.size is not None:
                    if actual_size != array_type.size:
                        self.error(f"Array '{vardecl.name}' declared with size {array_type.size} but initialized with {actual_size} elements")
                else:
                    # Infer size from literal (script mode)
                    array_type.size = actual_size
            else:
                self.error(f"Array variable '{vardecl.name}' must be initialized with an array literal or copy expression")
            
            # Update element type from literal if it was coerced
            vardecl.init.element_type = element_type
        else:
            # No initializer - size must be specified
            if array_type.size is None:
                self.error(f"Array '{vardecl.name}' must be initialized or have explicit size")
        
        # Define symbol with array information
        # Array takes element_size * count bytes on stack
        if is_field_element:
            element_size = self.get_field_size(element_type)
        elif element_type in TYPE_SIZES:
            element_size = TYPE_SIZES[element_type]
        else:
            element_size = 8
        total_size = element_size * array_type.size
        
        # Create symbol
        symbol = self.define_array_symbol(vardecl.name, array_type, vardecl.mutable, total_size)
        symbol.is_field_array = is_field_element  # Mark as array of fields
        
        # AST annotieren
        vardecl.stack_offset = symbol.stack_offset
        vardecl.symbol = symbol
        vardecl.total_size = total_size
        vardecl.is_field_array = is_field_element
    
    def define_array_symbol(self, name: str, array_type, mutable: bool, total_size: int) -> Symbol:
        """Define an array symbol with proper stack allocation"""
        # Allocate stack space
        alignment = 8  # Always align to 8 bytes for arrays
        self.current_stack_offset = align_offset(self.current_stack_offset, alignment)
        self.current_stack_offset += total_size
        
        symbol = Symbol(
            name=name,
            type='array',
            mutable=mutable,
            stack_offset=-self.current_stack_offset,
            array_type=array_type
        )
        
        self.current_scope.define(symbol)
        return symbol
    
    def analyze_assignment(self, assign: Assignment):
        # Handle array index assignment: arr[i] = value
        if isinstance(assign.target, IndexAccess):
            self.analyze_index_assignment(assign)
            return
        
        # Handle field access assignment: obj.member = value
        if isinstance(assign.target, FieldAccess):
            self.analyze_field_assignment(assign)
            return
        
        # Pointer dereference removed in v1.1.0 - AXIS uses value semantics
        
        # Target muss Identifier sein
        if not isinstance(assign.target, Identifier):
            self.error("Assignment target must be an identifier, array index, or field access")
        
        # Symbol lookup
        symbol = self.lookup_symbol(assign.target.name)
        
        # Mutable-Check
        if not symbol.mutable:
            self.error(f"Cannot assign to immutable variable: {symbol.name}")
        
        # Type-Check
        target_type = symbol.type
        
        # Check for array assignment - must use 'copy' keyword
        if target_type == 'array':
            if not isinstance(assign.value, CopyExpr):
                self.error(f"Array assignment requires 'copy' keyword: {symbol.name} = copy <source_array>")
        
        # Special handling for read expressions - pass target type
        if isinstance(assign.value, Read):
            value_type = self.analyze_read(assign.value, target_type)
        elif isinstance(assign.value, Readln):
            value_type = self.analyze_readln(assign.value, target_type)
        elif isinstance(assign.value, Readchar):
            value_type = self.analyze_readchar(assign.value, target_type)
        else:
            value_type = self.analyze_expression(assign.value)
        
        # Special case: Allow i32 literals to be assigned to other integer types if in range
        if target_type in ['i8', 'i16', 'i64', 'u8', 'u16', 'u32', 'u64'] and value_type == 'i32':
            if isinstance(assign.value, Literal):
                # Allow literal coercion
                assign.value.inferred_type = target_type
                value_type = target_type
            else:
                self.error(f"Type mismatch in assignment to '{symbol.name}': expected {target_type}, got {value_type}")
        # Special case: Allow i32 literals 0/1 to be assigned to bool
        elif target_type == 'bool' and value_type == 'i32':
            if isinstance(assign.value, Literal) and assign.value.value in ['0', '1']:
                # Convert literal type to bool
                assign.value.inferred_type = 'bool'
                value_type = 'bool'
            else:
                self.error(f"Cannot assign non-boolean value to bool variable '{symbol.name}'")
        # Pointer type removed in v1.1.0 - AXIS uses value semantics
        elif target_type != value_type:
            self.error(f"Type mismatch in assignment to '{symbol.name}': expected {target_type}, got {value_type}")
        
        # AST annotieren
        assign.target.symbol = symbol
        assign.target.inferred_type = target_type
    
    def analyze_index_assignment(self, assign: Assignment):
        """Analyze array index assignment: arr[i] = value"""
        idx_access = assign.target
        
        # Analyze the index access to get element type
        element_type = self.analyze_index_access(idx_access)
        
        # Analyze the value
        value_type = self.analyze_expression(assign.value)
        
        # Type check - allow literal coercion
        if value_type == 'i32' and element_type in ['i8', 'i16', 'i64', 'u8', 'u16', 'u32', 'u64']:
            if isinstance(assign.value, Literal):
                assign.value.inferred_type = element_type
                value_type = element_type
            else:
                self.error(f"Type mismatch in array element assignment: expected {element_type}, got {value_type}")
        elif value_type != element_type:
            self.error(f"Type mismatch in array element assignment: expected {element_type}, got {value_type}")
    
    def analyze_field_assignment(self, assign: Assignment):
        """Analyze field member assignment: obj.member = value"""
        field_access = assign.target
        
        # Analyze the field access to get member type
        member_type = self.analyze_field_access(field_access)
        
        # Analyze the value
        value_type = self.analyze_expression(assign.value)
        
        # Type check - allow literal coercion
        if value_type == 'i32' and member_type in ['i8', 'i16', 'i64', 'u8', 'u16', 'u32', 'u64']:
            if isinstance(assign.value, Literal):
                assign.value.inferred_type = member_type
                value_type = member_type
            else:
                self.error(f"Type mismatch in field assignment: expected {member_type}, got {value_type}")
        elif value_type != member_type:
            self.error(f"Type mismatch in field assignment: expected {member_type}, got {value_type}")
    
    # analyze_deref_assignment removed in v1.1.0 - AXIS uses value semantics
    
    def analyze_return(self, ret: Return):
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
        # Condition must be bool type (strict checking)
        cond_type = self.analyze_expression(if_stmt.condition)
        
        if cond_type != 'bool':
            self.error(f"Condition in 'when' statement must be bool type, got {cond_type}")
        
        # Then-Block analysieren
        self.analyze_block(if_stmt.then_block)
        
        # Else-Block analysieren
        if if_stmt.else_block:
            self.analyze_block(if_stmt.else_block)
    
    def analyze_while(self, while_stmt: While):
        # Condition must be bool type (strict checking)
        cond_type = self.analyze_expression(while_stmt.condition)
        
        if cond_type != 'bool':
            self.error(f"Condition in 'while' statement must be bool type, got {cond_type}")
        
        # Body analysieren (in Loop-Kontext)
        self.in_loop += 1
        self.analyze_block(while_stmt.body)
        self.in_loop -= 1
    
    def analyze_match(self, match_stmt: Match):
        """Analyze a match statement"""
        # Analyze the value being matched
        value_type = self.analyze_expression(match_stmt.value)
        match_stmt.value_type = value_type
        
        has_wildcard = False
        
        for arm in match_stmt.arms:
            if arm.is_wildcard:
                has_wildcard = True
            else:
                # Analyze the pattern and check type compatibility
                pattern_type = self.analyze_expression(arm.pattern)
                arm.pattern_type = pattern_type
                
                # Pattern type must match value type
                if pattern_type != value_type:
                    # Allow integer literal coercion
                    if not (pattern_type == 'i32' and value_type in INTEGER_TYPES):
                        self.error(f"Match pattern type '{pattern_type}' does not match value type '{value_type}'")
            
            # Analyze the body
            self.enter_scope()
            self.analyze_block(arm.body)
            self.exit_scope()
    
    def analyze_break(self, break_stmt: Break):
        if self.in_loop == 0:
            self.error("Break outside of loop")
    
    def analyze_continue(self, continue_stmt: Continue):
        if self.in_loop == 0:
            self.error("Continue outside of loop")
    
    def analyze_write(self, write_stmt: Write):
        """write() und writeln() - akzeptiert str, integers, bool, and enums"""
        value_type = self.analyze_expression(write_stmt.value)
        
        # Check ob valid output type (allow enum types - they're printed as integers)
        if value_type not in VALID_TYPES and value_type not in self.enum_types:
            self.error(f"Cannot write value of type '{value_type}'")
        
        # Annotate für codegen
        write_stmt.value_type = value_type
    
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
        elif isinstance(expr, StringLiteral):
            return self.analyze_string_literal(expr)
        elif isinstance(expr, Read):
            return self.analyze_read(expr)
        elif isinstance(expr, Readln):
            return self.analyze_readln(expr)
        elif isinstance(expr, Readchar):
            return self.analyze_readchar(expr)
        elif isinstance(expr, ReadFailed):
            return self.analyze_read_failed(expr)
        elif isinstance(expr, ArrayLiteral):
            return self.analyze_array_literal(expr)
        elif isinstance(expr, IndexAccess):
            return self.analyze_index_access(expr)
        elif isinstance(expr, CopyExpr):
            return self.analyze_copy_expr(expr)
        elif isinstance(expr, FieldAccess):
            return self.analyze_field_access(expr)
        elif isinstance(expr, EnumAccess):
            return self.analyze_enum_access(expr)
        # Pointer expression types removed in v1.1.0 - AXIS uses value semantics
        else:
            self.error(f"Unknown expression type: {type(expr).__name__}")
    
    def analyze_literal(self, lit: Literal) -> str:
        # Integer Literal: Default-Typ = i32
        if lit.type == 'int':
            inferred_type = 'i32'
            lit.inferred_type = inferred_type
            return inferred_type
        
        # Boolean Literal: True/False → bool
        if lit.type == 'bool':
            inferred_type = 'bool'
            lit.inferred_type = inferred_type
            return inferred_type
        
        self.error(f"Unknown literal type: {lit.type}")
    
    def analyze_string_literal(self, string_lit: StringLiteral) -> str:
        """String literal - einfach str Typ zurückgeben"""
        string_lit.inferred_type = 'str'
        return 'str'
    
    def analyze_read(self, read_expr: 'Read', target_type: str = None) -> str:
        """
        read() - read until EOF
        Type depends on assignment target:
        - i8/i16/i32/i64/u8/u16/u32/u64: parse as integer
        - str: read as string
        """
        if target_type is None:
            # Default to str if no target type specified
            target_type = 'str'
        
        if target_type not in INTEGER_TYPES and target_type != 'str':
            self.error(f"read() can only be assigned to integer or str types, not {target_type}")
        
        read_expr.inferred_type = target_type
        read_expr.target_type = target_type
        return target_type
    
    def analyze_readln(self, readln_expr: 'Readln', target_type: str = None) -> str:
        """
        readln() - read one line until \\n
        Type depends on assignment target:
        - i8/i16/i32/i64/u8/u16/u32/u64: parse as integer
        - str: read as string (newline stripped)
        """
        if target_type is None:
            target_type = 'str'
        
        if target_type not in INTEGER_TYPES and target_type != 'str':
            self.error(f"readln() can only be assigned to integer or str types, not {target_type}")
        
        readln_expr.inferred_type = target_type
        readln_expr.target_type = target_type
        return target_type
    
    def analyze_readchar(self, readchar_expr: 'Readchar', target_type: str = None) -> str:
        """
        readchar() - read single byte, returns -1 for EOF
        Always returns i32 (to accommodate -1 for EOF)
        Cannot be assigned to str (compile error)
        """
        if target_type == 'str':
            self.error("readchar() cannot be assigned to str type - use read() or readln() instead")
        
        # Always i32 to handle -1 for EOF
        readchar_expr.inferred_type = 'i32'
        readchar_expr.target_type = 'i32'
        return 'i32'
    
    def analyze_read_failed(self, read_failed_expr: 'ReadFailed') -> str:
        """
        read_failed() - returns bool indicating if last read operation failed
        """
        read_failed_expr.inferred_type = 'bool'
        return 'bool'
    
    def analyze_identifier(self, ident: Identifier) -> str:
        symbol = self.lookup_symbol(ident.name)
        
        # AST annotieren
        ident.symbol = symbol
        ident.inferred_type = symbol.type
        
        return symbol.type
    
    def analyze_binaryop(self, binop: BinaryOp) -> str:
        left_type = self.analyze_expression(binop.left)
        right_type = self.analyze_expression(binop.right)
        
        # Allow i32 literals to coerce to match the other operand's type
        if left_type != right_type:
            # Try to coerce i32 literal to match the other type
            if left_type == 'i32' and isinstance(binop.left, Literal) and is_integer_type(right_type):
                binop.left.inferred_type = right_type
                left_type = right_type
            elif right_type == 'i32' and isinstance(binop.right, Literal) and is_integer_type(left_type):
                binop.right.inferred_type = left_type
                right_type = left_type
            else:
                self.error(f"Type mismatch in binary operation '{binop.op}': {left_type} vs {right_type}")
        
        # Vergleichsoperatoren → bool
        if binop.op in ['==', '!=', '<', '<=', '>', '>=']:
            # Allow comparisons on integers, bools, and enums
            if not (is_integer_type(left_type) or left_type == 'bool' or left_type in self.enum_types):
                self.error(f"Comparison operator '{binop.op}' requires integer, bool, or enum types, got {left_type}")
            inferred_type = 'bool'
        
        # Arithmetik → gleicher Typ
        elif binop.op in ['+', '-', '*', '/', '%']:
            if not is_integer_type(left_type):
                self.error(f"Arithmetic operator '{binop.op}' requires integer types, got {left_type}")
            inferred_type = left_type
        
        # Bitweise Operationen → gleicher Typ
        elif binop.op in ['&', '|', '^']:
            if not is_integer_type(left_type):
                self.error(f"Bitwise operator '{binop.op}' requires integer types, got {left_type}")
            inferred_type = left_type
        
        # Shift operations: left operand type is result, right must be valid shift count
        elif binop.op in ['<<', '>>']:
            if not is_integer_type(left_type):
                self.error(f"Shift operator '{binop.op}' requires integer types, got {left_type}")
            if not is_integer_type(right_type):
                self.error(f"Shift count must be integer type, got {right_type}")
            
            # Warn if shift count is a literal and exceeds type bit width
            if isinstance(binop.right, Literal):
                shift_count = int(binop.right.value)
                type_bits = get_type_size(left_type) * 8
                
                if shift_count < 0:
                    self.error(f"Shift count cannot be negative: {shift_count}")
                elif shift_count >= type_bits:
                    # This is undefined behavior in C, but we'll allow it with a warning
                    # The hardware will typically mask the shift count (e.g., & 31 for i32)
                    pass  # Could add warning system here later
            
            inferred_type = left_type
        
        else:
            self.error(f"Unknown binary operator: {binop.op}")
        
        # AST annotieren
        binop.inferred_type = inferred_type
        return inferred_type
    
    def analyze_unaryop(self, unaryop: UnaryOp) -> str:
        operand_type = self.analyze_expression(unaryop.operand)
        
        if unaryop.op == '-':
            # Negation: nur signed integers
            if operand_type not in SIGNED_TYPES:
                self.error(f"Unary minus requires signed integer, got {operand_type}")
            inferred_type = operand_type
        
        elif unaryop.op == '!':
            # Boolean NOT: nur bool type
            if operand_type != 'bool':
                self.error(f"Unary '!' requires bool type, got {operand_type}")
            inferred_type = 'bool'
        
        else:
            self.error(f"Unknown unary operator: {unaryop.op}")
        
        # AST annotieren
        unaryop.inferred_type = inferred_type
        return inferred_type
    
    def analyze_call(self, call: Call) -> str:
        func_symbol = self.lookup_function(call.name)
        
        # Argument-Count prüfen
        if len(call.args) != len(func_symbol.params):
            self.error(f"Function '{call.name}' expects {len(func_symbol.params)} arguments, got {len(call.args)}")
        
        # Argument-Typen prüfen
        for i, (arg, param) in enumerate(zip(call.args, func_symbol.params)):
            arg_type = self.analyze_expression(arg)
            # param is now a Parameter object
            param_type = param.type if hasattr(param, 'type') else param[1]
            if arg_type != param_type:
                self.error(f"Argument {i+1} to function '{call.name}': expected {param_type}, got {arg_type}")
        
        # Return type - 'void' if not specified
        inferred_type = func_symbol.return_type if func_symbol.return_type else 'void'
        
        # AST annotieren
        call.inferred_type = inferred_type
        call.function_symbol = func_symbol
        
        return inferred_type
    
    # Pointer analysis functions removed in v1.1.0 - AXIS uses value semantics
    # analyze_deref, analyze_deref_with_context, analyze_addressof, 
    # analyze_sizeof, analyze_null_literal all removed
    
    def analyze_array_literal(self, arr_lit: ArrayLiteral, expected_element_type: str = None) -> str:
        """
        Analyze array literal [1, 2, 3].
        All elements must have the same type.
        Returns 'array' type and annotates with element_type and size.
        """
        if not arr_lit.elements:
            self.error("Empty array literals are not allowed")
        
        # Analyze first element to determine type
        first_type = self.analyze_expression(arr_lit.elements[0])
        
        # If we have an expected type from declaration, use it for coercion
        if expected_element_type:
            # Allow i32 literals to coerce to other integer types
            if first_type == 'i32' and expected_element_type in INTEGER_TYPES:
                if isinstance(arr_lit.elements[0], Literal):
                    arr_lit.elements[0].inferred_type = expected_element_type
                    first_type = expected_element_type
        
        element_type = first_type
        
        # Check all elements have the same type
        for i, elem in enumerate(arr_lit.elements[1:], start=1):
            elem_type = self.analyze_expression(elem)
            
            # Allow literal coercion
            if elem_type == 'i32' and element_type in INTEGER_TYPES:
                if isinstance(elem, Literal):
                    elem.inferred_type = element_type
                    elem_type = element_type
            
            if elem_type != element_type:
                self.error(f"Array element {i} has type {elem_type}, expected {element_type}")
        
        # Annotate the array literal
        arr_lit.inferred_type = 'array'
        arr_lit.element_type = element_type
        arr_lit.size = len(arr_lit.elements)
        
        return 'array'
    
    def analyze_index_access(self, idx_access: IndexAccess) -> str:
        """
        Analyze array index access arr[i].
        Returns the element type of the array.
        """
        # Analyze the array expression
        array_type = self.analyze_expression(idx_access.array)
        
        element_type = None
        element_field_def = None
        
        # Get element type from the array identifier's symbol
        if isinstance(idx_access.array, Identifier):
            symbol = self.lookup_symbol(idx_access.array.name)
            if symbol.type != 'array':
                self.error(f"Cannot index non-array type: {symbol.type}")
            
            # Get element type from the symbol's array_type annotation
            if hasattr(symbol, 'array_type') and symbol.array_type:
                element_type = symbol.array_type.element_type
            else:
                self.error(f"Array '{idx_access.array.name}' has no element type information")
        elif isinstance(idx_access.array, FieldAccess):
            # Accessing array via field: obj.arr[i]
            if array_type != 'array':
                self.error(f"Cannot index non-array member")
            
            # Get element type from the member info
            if hasattr(idx_access.array, 'member_info'):
                member = idx_access.array.member_info
                if member.array_type:
                    element_type = member.array_type.element_type
                    if element_type == 'field' and member.inline_field:
                        element_field_def = member.inline_field
                else:
                    self.error("Array member has no element type")
            else:
                self.error("Array member has no type information")
        else:
            self.error("Only array variables and field members can be indexed")
        
        # Analyze the index - must be an integer type
        index_type = self.analyze_expression(idx_access.index)
        if index_type not in INTEGER_TYPES:
            self.error(f"Array index must be an integer type, got {index_type}")
        
        # If element is an inline field, store its definition for further access
        if element_field_def:
            idx_access.element_field_def = element_field_def
            element_type = 'field'
        
        # Annotate the expression
        idx_access.inferred_type = element_type
        idx_access.element_type = element_type
        
        return element_type

    def analyze_copy_expr(self, copy_expr: CopyExpr) -> str:
        """
        Analyze copy <expr> - explicit value/array copy
        Returns the same type as the operand
        """
        operand_type = self.analyze_expression(copy_expr.operand)
        copy_expr.inferred_type = operand_type
        copy_expr.is_copy = True  # Mark this as an explicit copy
        return operand_type
    
    def analyze_field_access(self, field_access: FieldAccess) -> str:
        """
        Analyze field member access: obj.member
        Returns the type of the member.
        Also handles enum access: EnumName.VariantName
        """
        # Check if this is an enum access (EnumName.VariantName)
        if isinstance(field_access.object, Identifier):
            enum_name = field_access.object.name
            if enum_name in self.enum_types:
                # This is an enum variant access
                enum_def = self.enum_types[enum_name]
                variant_name = field_access.member
                
                # Find the variant
                for variant in enum_def.variants:
                    if variant.name == variant_name:
                        # Convert FieldAccess to EnumAccess
                        field_access.__class__ = EnumAccess
                        field_access.enum_name = enum_name
                        field_access.variant_name = variant_name
                        field_access.inferred_type = enum_name
                        field_access.variant_value = variant.value
                        return enum_name
                
                self.error(f"Enum '{enum_name}' has no variant '{variant_name}'")
        
        # Analyze the object expression to get its type
        obj_type = self.analyze_expression(field_access.object)
        
        # Handle inline field types (stored in the object's attributes)
        field_def = None
        if obj_type in self.field_types:
            field_def = self.field_types[obj_type]
        elif obj_type == 'field':
            # This is an inline field - look for the field definition in various places
            if hasattr(field_access.object, 'inline_field_def'):
                # FieldAccess that returned an inline field
                field_def = field_access.object.inline_field_def
            elif hasattr(field_access.object, 'element_field_def'):
                # IndexAccess that returned an inline field element
                field_def = field_access.object.element_field_def
            elif isinstance(field_access.object, IndexAccess) and hasattr(field_access.object, 'element_field_def'):
                # Index into array of inline fields
                field_def = field_access.object.element_field_def
            else:
                self.error(f"Cannot access member '{field_access.member}' - inline field has no definition")
        elif obj_type == 'array' and hasattr(field_access.object, 'element_field_def'):
            # This is an access to an element of an array of inline fields
            field_def = field_access.object.element_field_def
        else:
            self.error(f"Cannot access member '{field_access.member}' of non-field type '{obj_type}'")
        
        # Find the member in the field definition
        member, member_type = self.find_member_and_type(field_def, field_access.member)
        if member is None:
            self.error(f"Field type has no member '{field_access.member}'")
        
        # If the member is an inline field, store its definition for further access
        if member.type == 'field' and member.inline_field:
            field_access.inline_field_def = member.inline_field
        elif member.type == 'array' and member.array_type and member.array_type.element_type == 'field':
            if member.inline_field:
                field_access.element_field_def = member.inline_field
        
        # Annotate the expression
        field_access.inferred_type = member_type
        field_access.field_def = field_def
        field_access.member_info = member
        
        return member_type
    
    def analyze_enum_access(self, enum_access: EnumAccess) -> str:
        """
        Analyze enum variant access: EnumName.VariantName
        Returns the enum type name.
        """
        enum_name = enum_access.enum_name
        variant_name = enum_access.variant_name
        
        if enum_name not in self.enum_types:
            self.error(f"Unknown enum type: {enum_name}")
        
        enum_def = self.enum_types[enum_name]
        
        for variant in enum_def.variants:
            if variant.name == variant_name:
                enum_access.inferred_type = enum_name
                enum_access.variant_value = variant.value
                return enum_name
        
        self.error(f"Enum '{enum_name}' has no variant '{variant_name}'")
    
    def find_member_and_type(self, field_def: 'FieldDef', member_name: str) -> tuple:
        """Find a member and its type in a field definition"""
        for member in field_def.members:
            if member.name == member_name:
                if member.type == 'field':
                    # Inline nested field
                    return (member, 'field')
                elif member.type == 'array':
                    return (member, 'array')
                elif member.type in self.field_types:
                    return (member, member.type)
                else:
                    return (member, member.type)
        return (None, None)

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
    
    # Pointer AST nodes removed in v1.1.0 - AXIS uses value semantics

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
