import re
import sys
from enum import Enum

class RegisterType(Enum):
    REG_8 = 1   # al, bl, cl, dl
    REG_16 = 2  # ax, bx, cx, dx
    REG_32 = 3  # eax, ebx, ecx, edx
    REG_64 = 4  # rax, rbx, rcx, rdx

class Assembler:
    def __init__(self):
        # register tables - alle x86-64 register hier
        self.reg8 = {'al': 0, 'cl': 1, 'dl': 2, 'bl': 3, 'ah': 4, 'ch': 5, 'dh': 6, 'bh': 7}
        self.reg8_rex = {'spl': 4, 'bpl': 5, 'sil': 6, 'dil': 7, 'r8b': 8, 'r9b': 9, 'r10b': 10, 'r11b': 11, 
                         'r12b': 12, 'r13b': 13, 'r14b': 14, 'r15b': 15}
        
        self.reg16 = {'ax': 0, 'cx': 1, 'dx': 2, 'bx': 3, 'sp': 4, 'bp': 5, 'si': 6, 'di': 7,
                      'r8w': 8, 'r9w': 9, 'r10w': 10, 'r11w': 11, 'r12w': 12, 'r13w': 13, 'r14w': 14, 'r15w': 15}
        
        self.reg32 = {'eax': 0, 'ecx': 1, 'edx': 2, 'ebx': 3, 'esp': 4, 'ebp': 5, 'esi': 6, 'edi': 7,
                      'r8d': 8, 'r9d': 9, 'r10d': 10, 'r11d': 11, 'r12d': 12, 'r13d': 13, 'r14d': 14, 'r15d': 15}
        
        self.reg64 = {'rax': 0, 'rcx': 1, 'rdx': 2, 'rbx': 3, 'rsp': 4, 'rbp': 5, 'rsi': 6, 'rdi': 7,
                      'r8': 8, 'r9': 9, 'r10': 10, 'r11': 11, 'r12': 12, 'r13': 13, 'r14': 14, 'r15': 15}
        
        self.labels = {}
        self.current_address = 0
        self.jump_forms = {}
        self.current_instr_index = 0
        # jump forms tracking für relaxation - wichtig dass index-based ist!
        
    def get_reg_num(self, reg):
        reg = reg.lower()
        if reg in self.reg8 or reg in self.reg8_rex:
            num = self.reg8.get(reg, self.reg8_rex.get(reg))
            return num, RegisterType.REG_8
        elif reg in self.reg16:
            return self.reg16[reg], RegisterType.REG_16
        elif reg in self.reg32:
            return self.reg32[reg], RegisterType.REG_32
        elif reg in self.reg64:
            return self.reg64[reg], RegisterType.REG_64
        return None, None
    
    def is_high_byte_reg(self, reg):
        return reg.lower() in ['ah', 'ch', 'dh', 'bh']
    
    def validate_8bit_regs(self, reg1, reg2=None):
        # AH/CH/DH/BH können nicht mit REX-Prefix verwendet werden
        regs = [reg1] if reg2 is None else [reg1, reg2]
        has_high_byte = any(self.is_high_byte_reg(r) for r in regs if r)
        has_rex_reg = any(r and r.lower() in self.reg8_rex for r in regs if r)
        
        if has_high_byte and has_rex_reg:
            return False
        return True
    
    def is_immediate(self, value_str):
        try:
            self.parse_immediate(value_str)
            return True
        except (ValueError, AttributeError):
            return False
    
    def parse_immediate(self, value_str):
        value_str = value_str.strip()
        if not value_str:
            raise ValueError("Leerer Immediate-Wert")
        
        negative = value_str.startswith('-')
        if negative:
            value_str = value_str[1:]
            if not value_str:
                raise ValueError("Ungültiger Immediate-Wert: -")
        
        try:
            if value_str.startswith('0x') or value_str.startswith('0X'):
                result = int(value_str, 16)
            elif value_str.startswith('0b') or value_str.startswith('0B'):
                result = int(value_str, 2)
            else:
                result = int(value_str, 10)
        except ValueError as e:
            raise ValueError(f"Ungültiger Immediate-Wert: {value_str}")
        
        return -result if negative else result
    
    def validate_immediate_size(self, value, bits):
        if bits == 8:
            return -128 <= value <= 255
        elif bits == 16:
            return -32768 <= value <= 65535
        elif bits == 32:
            return -2147483648 <= value <= 4294967295
        elif bits == 64:
            return -9223372036854775808 <= value <= 18446744073709551615
        return False
    
    def encode_modrm(self, mod, reg, rm):
        return ((mod & 0x3) << 6) | ((reg & 0x7) << 3) | (rm & 0x7)
    
    def needs_rex(self, reg_num):
        return reg_num >= 8
    
    def build_rex(self, w=0, r=0, x=0, b=0):
        return 0x40 | (w << 3) | (r << 2) | (x << 1) | b
    
    def parse_memory_operand(self, operand):
        operand = operand.strip()
        if not operand.startswith('[') or not operand.endswith(']'):
            return None
        
        inner = operand[1:-1].strip()
        
        if not inner.startswith('rbp'):
            return None
        
        # Parse [rbp+offset] oder [rbp-offset]
        if '+' in inner:
            parts = inner.split('+')
            if len(parts) != 2 or parts[0].strip() != 'rbp':
                return None
            offset = self.parse_immediate(parts[1].strip())
        elif '-' in inner:
            parts = inner.split('-')
            if len(parts) != 2 or parts[0].strip() != 'rbp':
                return None
            offset = -self.parse_immediate(parts[1].strip())
        else:
            if inner.strip() == 'rbp':
                offset = 0
            else:
                return None
        
        return ('rbp', offset)
    
    def assemble_mov(self, dest, src):
        # mov instruction - viele verschiedene forms
        bytecode = []
        
        # Handle mov rax, qword [rbp-X] (qword load with explicit size)
        if src.startswith('qword'):
            src_mem = src[5:].strip()  # Remove "qword"
            mem_op = self.parse_memory_operand(src_mem)
            if mem_op and mem_op[0] == 'rbp':
                dest_num, dest_type = self.get_reg_num(dest)
                if dest_num is None or dest_type != RegisterType.REG_64:
                    return None
                
                base, disp = mem_op
                use_disp8 = -128 <= disp <= 127
                mod = 0x01 if use_disp8 else 0x02
                
                modrm = self.encode_modrm(mod, dest_num % 8, 0b101)
                
                # MOV r64, r/m64: REX.W 8B /r
                rex = self.build_rex(w=1, r=(dest_num >= 8))
                bytecode = [rex, 0x8B, modrm]
                
                if use_disp8:
                    bytecode.append(disp & 0xFF)
                else:
                    bytecode.extend(list(disp.to_bytes(4, 'little', signed=True)))
                
                return bytecode
            return None
        
        # mov reg, [memory]
        if src.startswith('['):
            mem_op = self.parse_memory_operand(src)
            if mem_op and mem_op[0] == 'rbp':
                dest_num, dest_type = self.get_reg_num(dest)
                if dest_num is None:
                    return None
                
                base, disp = mem_op
                
                # Bestimme ob disp8 oder disp32
                use_disp8 = -128 <= disp <= 127
                mod = 0x01 if use_disp8 else 0x02
                
                modrm = self.encode_modrm(mod, dest_num % 8, 0b101)
                
                if dest_type == RegisterType.REG_32:
                    bytecode = [0x8B, modrm]
                    if dest_num >= 8:
                        bytecode = [0x41] + bytecode
                    
                    if use_disp8:
                        bytecode.append(disp & 0xFF)
                    else:
                        bytecode.extend(list(disp.to_bytes(4, 'little', signed=True)))
                
                elif dest_type == RegisterType.REG_64:
                    rex = self.build_rex(w=1, r=(dest_num >= 8))
                    bytecode = [rex, 0x8B, modrm]
                    
                    if use_disp8:
                        bytecode.append(disp & 0xFF)
                    else:
                        bytecode.extend(list(disp.to_bytes(4, 'little', signed=True)))
                
                return bytecode
            
            return None
        
        # Handle mov byte [rbp-X], al (byte store)
        if dest.startswith('byte'):
            dest_mem = dest[4:].strip()  # Remove "byte"
            mem_op = self.parse_memory_operand(dest_mem)
            if mem_op and mem_op[0] == 'rbp':
                src_num, src_type = self.get_reg_num(src)
                if src_num is None:
                    return None
                
                base, disp = mem_op
                use_disp8 = -128 <= disp <= 127
                mod = 0x01 if use_disp8 else 0x02
                
                modrm = self.encode_modrm(mod, src_num % 8, 0b101)
                
                # MOV r/m8, r8: opcode 88
                bytecode = [0x88, modrm]
                # For al/bl/cl/dl (0-3) no REX needed
                # For spl/bpl/sil/dil (4-7) need REX prefix without any bits set
                if src_num >= 4:
                    bytecode = [0x40] + bytecode
                
                if use_disp8:
                    bytecode.append(disp & 0xFF)
                else:
                    bytecode.extend(list(disp.to_bytes(4, 'little', signed=True)))
                
                return bytecode
            return None
        
        # Handle mov word [rbp-X], ax (word store)
        if dest.startswith('word'):
            dest_mem = dest[4:].strip()  # Remove "word"
            mem_op = self.parse_memory_operand(dest_mem)
            if mem_op and mem_op[0] == 'rbp':
                src_num, src_type = self.get_reg_num(src)
                if src_num is None:
                    return None
                
                base, disp = mem_op
                use_disp8 = -128 <= disp <= 127
                mod = 0x01 if use_disp8 else 0x02
                
                modrm = self.encode_modrm(mod, src_num % 8, 0b101)
                
                # MOV r/m16, r16: 66 89 /r (operand size prefix + opcode)
                bytecode = [0x66, 0x89, modrm]
                
                if use_disp8:
                    bytecode.append(disp & 0xFF)
                else:
                    bytecode.extend(list(disp.to_bytes(4, 'little', signed=True)))
                
                return bytecode
            return None
        
        # Handle mov qword [rbp-X], rax (qword store)
        if dest.startswith('qword'):
            dest_mem = dest[5:].strip()  # Remove "qword"
            mem_op = self.parse_memory_operand(dest_mem)
            if mem_op and mem_op[0] == 'rbp':
                src_num, src_type = self.get_reg_num(src)
                if src_num is None:
                    return None
                
                base, disp = mem_op
                use_disp8 = -128 <= disp <= 127
                mod = 0x01 if use_disp8 else 0x02
                
                modrm = self.encode_modrm(mod, src_num % 8, 0b101)
                
                # MOV r/m64, r64: REX.W 89 /r
                rex = self.build_rex(w=1, r=(src_num >= 8))
                bytecode = [rex, 0x89, modrm]
                
                if use_disp8:
                    bytecode.append(disp & 0xFF)
                else:
                    bytecode.extend(list(disp.to_bytes(4, 'little', signed=True)))
                
                return bytecode
            return None
        
        if dest.startswith('['):
            mem_op = self.parse_memory_operand(dest)
            if mem_op and mem_op[0] == 'rbp':
                src_num, src_type = self.get_reg_num(src)
                if src_num is None:
                    return None
                
                base, disp = mem_op
                
                use_disp8 = -128 <= disp <= 127
                mod = 0x01 if use_disp8 else 0x02
                
                modrm = self.encode_modrm(mod, src_num % 8, 0b101)
                
                if src_type == RegisterType.REG_32:
                    bytecode = [0x89, modrm]
                    if src_num >= 8:
                        bytecode = [0x41] + bytecode
                    
                    if use_disp8:
                        bytecode.append(disp & 0xFF)
                    else:
                        bytecode.extend(list(disp.to_bytes(4, 'little', signed=True)))
                
                elif src_type == RegisterType.REG_64:
                    rex = self.build_rex(w=1, r=(src_num >= 8))
                    bytecode = [rex, 0x89, modrm]
                    
                    if use_disp8:
                        bytecode.append(disp & 0xFF)
                    else:
                        bytecode.extend(list(disp.to_bytes(4, 'little', signed=True)))
                
                return bytecode
            
            return None
        
        if not src.startswith('['):
            dest_num, dest_type = self.get_reg_num(dest)
            if dest_num is not None and self.is_immediate(src):
                imm = self.parse_immediate(src)
                
                if dest_type == RegisterType.REG_32:
                    if not self.validate_immediate_size(imm, 32):
                        return None
                    try:
                        bytecode = [0xB8 + (dest_num % 8)] + list(imm.to_bytes(4, 'little', signed=True))
                    except OverflowError:
                        return None
                    if dest_num >= 8:
                        bytecode = [0x41] + bytecode
                        
                elif dest_type == RegisterType.REG_64:
                    if not self.validate_immediate_size(imm, 64):
                        return None
                    rex = self.build_rex(w=1, b=(dest_num >= 8))
                    try:
                        bytecode = [rex, 0xB8 + (dest_num % 8)] + list(imm.to_bytes(8, 'little', signed=True))
                    except OverflowError:
                        return None
                    
                elif dest_type == RegisterType.REG_16:
                    if not self.validate_immediate_size(imm, 16):
                        return None
                    try:
                        bytecode = [0x66, 0xB8 + (dest_num % 8)] + list(imm.to_bytes(2, 'little', signed=True))
                    except OverflowError:
                        return None
                    if dest_num >= 8:
                        bytecode.insert(1, 0x41)
                        
                elif dest_type == RegisterType.REG_8:
                    if not self.validate_immediate_size(imm, 8):
                        return None
                    if dest_num >= 8 or dest.lower() in self.reg8_rex:
                        if self.is_high_byte_reg(dest):
                            return None
                            return None  # Ungültige Kombination
                        rex = self.build_rex(b=(dest_num >= 8))
                        bytecode = [rex, 0xB0 + (dest_num % 8), imm & 0xFF]
                    else:
                        bytecode = [0xB0 + dest_num, imm & 0xFF]
                        
                return bytecode
        

        dest_num, dest_type = self.get_reg_num(dest)
        src_num, src_type = self.get_reg_num(src)
        
        if dest_num is not None and src_num is not None and dest_type == src_type:
            modrm = self.encode_modrm(3, src_num % 8, dest_num % 8)
            
            if dest_type == RegisterType.REG_32:
                bytecode = [0x89, modrm]
                if dest_num >= 8 or src_num >= 8:
                    rex = self.build_rex(r=(src_num >= 8), b=(dest_num >= 8))
                    bytecode = [rex] + bytecode
                    
            elif dest_type == RegisterType.REG_64:
                rex = self.build_rex(w=1, r=(src_num >= 8), b=(dest_num >= 8))
                bytecode = [rex, 0x89, modrm]
                
            elif dest_type == RegisterType.REG_16:
                bytecode = [0x66, 0x89, modrm]
                if dest_num >= 8 or src_num >= 8:
                    rex = self.build_rex(r=(src_num >= 8), b=(dest_num >= 8))
                    bytecode.insert(1, rex)
                    
            elif dest_type == RegisterType.REG_8:
                # Validierung: AH/CH/DH/BH können nicht mit REX-Registern gemischt werden
                if not self.validate_8bit_regs(dest, src):
                    return None  # Ungültige Kombination
                
                bytecode = [0x88, modrm]
                if dest_num >= 8 or src_num >= 8 or dest.lower() in self.reg8_rex or src.lower() in self.reg8_rex:
                    rex = self.build_rex(r=(src_num >= 8), b=(dest_num >= 8))
                    bytecode = [rex] + bytecode
                    
            return bytecode
        
        return None
    
    def assemble_alu(self, operation, dest, src):
        ops = {'add': 0, 'or': 1, 'and': 4, 'sub': 5, 'xor': 6, 'cmp': 7}
        if operation not in ops:
            return None
            
        op_code = ops[operation]
        bytecode = []
        
        dest_num, dest_type = self.get_reg_num(dest)
        src_num, src_type = self.get_reg_num(src) if not self.is_immediate(src) else (None, None)
        
        if dest_num is not None and src_num is not None and dest_type == src_type:
            modrm = self.encode_modrm(3, src_num % 8, dest_num % 8)
            
            if dest_type == RegisterType.REG_32:
                bytecode = [0x01 + op_code * 8, modrm]
                if dest_num >= 8 or src_num >= 8:
                    rex = self.build_rex(r=(src_num >= 8), b=(dest_num >= 8))
                    bytecode = [rex] + bytecode
                    
            elif dest_type == RegisterType.REG_64:
                rex = self.build_rex(w=1, r=(src_num >= 8), b=(dest_num >= 8))
                bytecode = [rex, 0x01 + op_code * 8, modrm]
                
        elif dest_num is not None and self.is_immediate(src):
            imm = self.parse_immediate(src)
            
            if dest_type == RegisterType.REG_32:
                if -128 <= imm <= 127:
                    modrm = self.encode_modrm(3, op_code, dest_num % 8)
                    bytecode = [0x83, modrm, imm & 0xFF]
                    if dest_num >= 8:
                        bytecode = [0x41] + bytecode
                else:
                    if not self.validate_immediate_size(imm, 32):
                        return None
                    modrm = self.encode_modrm(3, op_code, dest_num % 8)
                    try:
                        bytecode = [0x81, modrm] + list(imm.to_bytes(4, 'little', signed=True))
                    except OverflowError:
                        return None
                    if dest_num >= 8:
                        bytecode = [0x41] + bytecode
                        
            elif dest_type == RegisterType.REG_64:
                if -128 <= imm <= 127:
                    rex = self.build_rex(w=1, b=(dest_num >= 8))
                    modrm = self.encode_modrm(3, op_code, dest_num % 8)
                    bytecode = [rex, 0x83, modrm, imm & 0xFF]
                else:
                    if not (-2147483648 <= imm <= 2147483647):
                        return None
                    rex = self.build_rex(w=1, b=(dest_num >= 8))
                    modrm = self.encode_modrm(3, op_code, dest_num % 8)
                    try:
                        bytecode = [rex, 0x81, modrm] + list(imm.to_bytes(4, 'little', signed=True))
                    except OverflowError:
                        return None
                    
        return bytecode
    
    def assemble_push_pop(self, operation, operand):
        bytecode = []
        reg_num, reg_type = self.get_reg_num(operand)
        
        if reg_num is not None:
            if reg_type == RegisterType.REG_8:
                return None
            
            if operation == 'push':
                bytecode = [0x50 + (reg_num % 8)]
                if reg_num >= 8:
                    bytecode = [0x41] + bytecode
            elif operation == 'pop':
                bytecode = [0x58 + (reg_num % 8)]
                if reg_num >= 8:
                    bytecode = [0x41] + bytecode
                    
        return bytecode
    
    def assemble_jmp_call(self, operation, target, force_form=None):
        bytecode = []
        
        if operation == 'call':
            force_form = 'near'
        
        if self.is_immediate(target):
            offset = self.parse_immediate(target)
            if operation == 'jmp':
                if force_form == 'short' or (force_form is None and -128 <= offset <= 127):
                    bytecode = [0xEB, offset & 0xFF]
                else:
                    try:
                        bytecode = [0xE9] + list(offset.to_bytes(4, 'little', signed=True))
                    except OverflowError:
                        return None
            elif operation == 'call':
                try:
                    bytecode = [0xE8] + list(offset.to_bytes(4, 'little', signed=True))
                except OverflowError:
                    return None
        # Wenn target ein Label ist
        else:
            if target in self.labels:
                # Form bestimmen (aus jump_forms dict oder auto)
                form = force_form
                if form is None:
                    form = self.jump_forms.get(self.current_instr_index, 'near')
                
                if operation == 'jmp':
                    if form == 'short':
                        offset = self.labels[target] - (self.current_address + 2)
                        if not (-128 <= offset <= 127):
                            return None
                        bytecode = [0xEB, offset & 0xFF]
                    else:
                        offset = self.labels[target] - (self.current_address + 5)
                        try:
                            bytecode = [0xE9] + list(offset.to_bytes(4, 'little', signed=True))
                        except OverflowError:
                            return None
                elif operation == 'call':
                    offset = self.labels[target] - (self.current_address + 5)
                    try:
                        bytecode = [0xE8] + list(offset.to_bytes(4, 'little', signed=True))
                    except OverflowError:
                        return None
            else:
                if operation == 'jmp':
                    bytecode = [0xE9, 0x00, 0x00, 0x00, 0x00]
                elif operation == 'call':
                    bytecode = [0xE8, 0x00, 0x00, 0x00, 0x00]
                
        return bytecode
    
    def assemble_conditional_jmp(self, operation, target, force_form=None):
        jmp_codes = {
            'je': 0x84, 'jz': 0x84, 'jne': 0x85, 'jnz': 0x85,
            'jl': 0x8C, 'jnge': 0x8C, 'jle': 0x8E, 'jng': 0x8E,
            'jg': 0x8F, 'jnle': 0x8F, 'jge': 0x8D, 'jnl': 0x8D,
            'ja': 0x87, 'jnbe': 0x87, 'jae': 0x83, 'jnb': 0x83,
            'jb': 0x82, 'jnae': 0x82, 'jbe': 0x86, 'jna': 0x86
        }
        
        if operation not in jmp_codes:
            return None
        
        opcode = jmp_codes[operation]
        
        if self.is_immediate(target):
            offset = self.parse_immediate(target)
            if force_form == 'short' or (force_form is None and -128 <= offset <= 127):
                return [opcode - 0x10, offset & 0xFF]
            else:
                try:
                    return [0x0F, opcode] + list(offset.to_bytes(4, 'little', signed=True))
                except OverflowError:
                    return None
        # Wenn target ein Label ist
        else:
            if target in self.labels:
                # Form bestimmen (aus jump_forms dict oder auto)
                form = force_form
                if form is None:
                    form = self.jump_forms.get(self.current_instr_index, 'near')
                
                if form == 'short':
                    offset = self.labels[target] - (self.current_address + 2)
                    if -128 <= offset <= 127:
                        return [opcode - 0x10, offset & 0xFF]
                    # Fall back to near form if short doesn't fit
                    form = 'near'
                
                if form == 'near':
                    offset = self.labels[target] - (self.current_address + 6)
                    try:
                        return [0x0F, opcode] + list(offset.to_bytes(4, 'little', signed=True))
                    except OverflowError:
                        return None
            else:
                return [0x0F, opcode, 0x00, 0x00, 0x00, 0x00]
        
        return None
    
    def assemble_single(self, operation):
        singles = {
            'ret': [0xC3], 'nop': [0x90], 'int3': [0xCC],
            'syscall': [0x0F, 0x05], 'leave': [0xC9],
            'pushf': [0x9C], 'popf': [0x9D],
            'cdq': [0x99], 'cqo': [0x48, 0x99]
        }
        return singles.get(operation, None)
    
    def assemble_inc_dec(self, operation, operand):
        reg_num, reg_type = self.get_reg_num(operand)
        if reg_num is None:
            return None
            
        bytecode = []
        op = 0xC0 if operation == 'inc' else 0xC8
        
        if reg_type == RegisterType.REG_32:
            modrm = op + (reg_num % 8)
            bytecode = [0xFF, modrm]
            if reg_num >= 8:
                bytecode = [0x41] + bytecode
        elif reg_type == RegisterType.REG_64:
            rex = self.build_rex(w=1, b=(reg_num >= 8))
            modrm = op + (reg_num % 8)
            bytecode = [rex, 0xFF, modrm]
            
        return bytecode
    
    def assemble_neg(self, operand):
        # neg eax/rax: F7 D8 (two's complement negation)
        reg_num, reg_type = self.get_reg_num(operand)
        if reg_num is None:
            return None
        
        if reg_type == RegisterType.REG_32:
            # NEG r32: F7 /3 (opcode extension 3)
            modrm = self.encode_modrm(3, 3, reg_num % 8)
            bytecode = [0xF7, modrm]
            if reg_num >= 8:
                bytecode = [0x41] + bytecode
        elif reg_type == RegisterType.REG_64:
            # NEG r64: REX.W F7 /3
            rex = self.build_rex(w=1, b=(reg_num >= 8))
            modrm = self.encode_modrm(3, 3, reg_num % 8)
            bytecode = [rex, 0xF7, modrm]
        else:
            return None
        
        return bytecode
    
    def assemble_movsx(self, dest, src):
        # movsx eax, byte/word [rbp-X] - sign extend byte/word to dword
        dest_num, dest_type = self.get_reg_num(dest)
        if dest_num is None or dest_type != RegisterType.REG_32:
            return None
        
        # Parse byte [rbp-X] or word [rbp-X] format
        if src.startswith('byte'):
            size_prefix = 'byte'
            opcode = 0xBE  # MOVSX r32, r/m8: 0F BE /r
            src = src[4:].strip()
        elif src.startswith('word'):
            size_prefix = 'word'
            opcode = 0xBF  # MOVSX r32, r/m16: 0F BF /r
            src = src[4:].strip()
        else:
            return None
        
        mem_op = self.parse_memory_operand(src)
        if mem_op is None or mem_op[0] != 'rbp':
            return None
        
        base, disp = mem_op
        use_disp8 = -128 <= disp <= 127
        mod = 0x01 if use_disp8 else 0x02
        
        # ModR/M: mod=01/10, reg=dest, rm=101 (rbp)
        modrm = self.encode_modrm(mod, dest_num % 8, 0b101)
        
        bytecode = [0x0F, opcode, modrm]
        if dest_num >= 8:
            bytecode = [0x44] + bytecode  # REX.R
        
        if use_disp8:
            bytecode.append(disp & 0xFF)
        else:
            bytecode.extend(list(disp.to_bytes(4, 'little', signed=True)))
        
        return bytecode
    
    def assemble_movzx(self, dest, src):
        # movzx eax, byte/word [rbp-X] - zero extend byte/word to dword
        dest_num, dest_type = self.get_reg_num(dest)
        if dest_num is None or dest_type != RegisterType.REG_32:
            return None
        
        # Parse byte [rbp-X] or word [rbp-X] format
        if src.startswith('byte'):
            size_prefix = 'byte'
            opcode = 0xB6  # MOVZX r32, r/m8: 0F B6 /r
            src = src[4:].strip()
        elif src.startswith('word'):
            size_prefix = 'word'
            opcode = 0xB7  # MOVZX r32, r/m16: 0F B7 /r
            src = src[4:].strip()
        else:
            return None
        
        mem_op = self.parse_memory_operand(src)
        if mem_op is None or mem_op[0] != 'rbp':
            return None
        
        base, disp = mem_op
        use_disp8 = -128 <= disp <= 127
        mod = 0x01 if use_disp8 else 0x02
        
        # ModR/M: mod=01/10, reg=dest, rm=101 (rbp)
        modrm = self.encode_modrm(mod, dest_num % 8, 0b101)
        
        bytecode = [0x0F, opcode, modrm]
        if dest_num >= 8:
            bytecode = [0x44] + bytecode  # REX.R
        
        if use_disp8:
            bytecode.append(disp & 0xFF)
        else:
            bytecode.extend(list(disp.to_bytes(4, 'little', signed=True)))
        
        return bytecode

    def assemble_imul_idiv(self, operation, operand):
        # imul/idiv für 32-bit: imul ecx, idiv ecx
        reg_num, reg_type = self.get_reg_num(operand)
        if reg_num is None or reg_type != RegisterType.REG_32:
            return None
        
        # imul ecx: 0F AF C1 (two-operand form: eax = eax * ecx)
        if operation == 'imul':
            rex = 0x41 if reg_num >= 8 else None
            modrm = self.encode_modrm(3, 0, reg_num % 8)  # eax * operand -> eax
            bytecode = [0x0F, 0xAF, modrm]
            if rex:
                bytecode = [rex] + bytecode
            return bytecode
        
        # idiv ecx: F7 F9 (signed divide edx:eax by ecx -> eax=quotient, edx=remainder)
        elif operation == 'idiv':
            rex = 0x41 if reg_num >= 8 else None
            modrm = self.encode_modrm(3, 7, reg_num % 8)  # /7 für idiv
            bytecode = [0xF7, modrm]
            if rex:
                bytecode = [rex] + bytecode
            return bytecode
        
        return None
    
    def assemble_shift(self, operation, dest, count):
        # Shift operations: shl/shr eax, cl oder shl/shr eax, imm
        dest_num, dest_type = self.get_reg_num(dest)
        if dest_num is None or dest_type != RegisterType.REG_32:
            return None
        
        # Determine shift type
        shift_ops = {'shl': 4, 'shr': 5, 'sal': 4, 'sar': 7}
        if operation not in shift_ops:
            return None
        
        shift_code = shift_ops[operation]
        
        # Check if count is 'cl' register or immediate
        if count.lower() == 'cl':
            # Shift by CL register: D3 /4 (shl) or D3 /5 (shr)
            modrm = self.encode_modrm(3, shift_code, dest_num % 8)
            bytecode = [0xD3, modrm]
            if dest_num >= 8:
                bytecode = [0x41] + bytecode
            return bytecode
        else:
            # Shift by immediate
            try:
                imm = int(count, 0)
                if imm == 1:
                    # Special encoding for shift by 1: D1 /4 or D1 /5
                    modrm = self.encode_modrm(3, shift_code, dest_num % 8)
                    bytecode = [0xD1, modrm]
                    if dest_num >= 8:
                        bytecode = [0x41] + bytecode
                elif 0 <= imm <= 255:
                    # Shift by immediate: C1 /4 imm8 or C1 /5 imm8
                    modrm = self.encode_modrm(3, shift_code, dest_num % 8)
                    bytecode = [0xC1, modrm, imm]
                    if dest_num >= 8:
                        bytecode = [0x41] + bytecode
                else:
                    return None
                return bytecode
            except ValueError:
                return None
        
        return None
    
    def assemble(self, instruction):
        instruction = instruction.strip()
        if not instruction or instruction.startswith(';'):
            return []
        
        if instruction.endswith(':'):
            label = instruction[:-1]
            self.labels[label] = self.current_address
            return []
        
        if ';' in instruction:
            instruction = instruction[:instruction.index(';')].strip()
        
        parts = re.split(r'[\s,]+', instruction.lower())
        operation = parts[0]
        
        bytecode = self.assemble_single(operation)
        if bytecode:
            return bytecode
        
        if len(parts) == 2:
            operand = parts[1]
            
            if operation in ['push', 'pop']:
                return self.assemble_push_pop(operation, operand) or []
            
            if operation in ['inc', 'dec']:
                return self.assemble_inc_dec(operation, operand) or []
            
            if operation == 'neg':
                return self.assemble_neg(operand) or []
            
            if operation in ['imul', 'idiv']:
                return self.assemble_imul_idiv(operation, operand) or []
            
            if operation in ['jmp', 'call']:
                return self.assemble_jmp_call(operation, operand) or []
            
            bytecode = self.assemble_conditional_jmp(operation, operand)
            if bytecode:
                return bytecode
        
        if len(parts) >= 3:
            dest = parts[1]
            src = ' '.join(parts[2:])
            
            # Handle "mov byte [rbp-X], al" - byte prefix on dest
            if operation == 'mov' and dest == 'byte' and len(parts) >= 4:
                dest = 'byte ' + parts[2]  # Reconstruct "byte [rbp-X]"
                src = ' '.join(parts[3:])
            
            # Handle "mov word [rbp-X], ax" - word prefix on dest
            if operation == 'mov' and dest == 'word' and len(parts) >= 4:
                dest = 'word ' + parts[2]  # Reconstruct "word [rbp-X]"
                src = ' '.join(parts[3:])
            
            # Handle "mov qword [rbp-X], rax" - qword prefix on dest
            if operation == 'mov' and dest == 'qword' and len(parts) >= 4:
                dest = 'qword ' + parts[2]  # Reconstruct "qword [rbp-X]"
                src = ' '.join(parts[3:])
            
            # Handle "mov rax, qword [rbp-X]" - qword prefix on src
            if operation == 'mov' and len(parts) >= 4 and parts[2] == 'qword':
                src = 'qword ' + ' '.join(parts[3:])
            
            if operation == 'mov':
                bytecode = self.assemble_mov(dest, src)
                if bytecode:
                    return bytecode
            
            # Handle "movsx eax, byte [rbp-X]" and "movzx eax, byte [rbp-X]"
            if operation == 'movsx':
                # Reconstruct "byte [rbp-X]" or "word [rbp-X]"
                if len(parts) >= 4 and parts[2] in ['byte', 'word']:
                    src = parts[2] + ' ' + ' '.join(parts[3:])
                bytecode = self.assemble_movsx(dest, src)
                if bytecode:
                    return bytecode
            
            if operation == 'movzx':
                # Reconstruct "byte [rbp-X]" or "word [rbp-X]"
                if len(parts) >= 4 and parts[2] in ['byte', 'word']:
                    src = parts[2] + ' ' + ' '.join(parts[3:])
                bytecode = self.assemble_movzx(dest, src)
                if bytecode:
                    return bytecode
            
            if operation in ['shl', 'shr', 'sal', 'sar']:
                bytecode = self.assemble_shift(operation, dest, src)
                if bytecode:
                    return bytecode
            
            if operation in ['add', 'sub', 'xor', 'or', 'and', 'cmp']:
                bytecode = self.assemble_alu(operation, dest, src)
                if bytecode:
                    return bytecode
        
        return None
    
    def assemble_code(self, code, enable_relaxation=True):
        # zwei-pass assembly mit optional relaxation für short jumps
        lines = [line.strip() for line in code.split('\n')]
        
        # pass 1: labels sammeln
        self.current_address = 0
        self.current_instr_index = 0
        self.labels = {}
        self.jump_forms = {}
        
        for line in lines:
            if line and not line.startswith(';'):
                if line.endswith(':'):
                    label_name = line[:-1]
                    if label_name in self.labels:
                        print(f"Warnung: Doppeltes Label '{label_name}' ignoriert")
                    else:
                        self.labels[label_name] = self.current_address
                else:
                    bytecode = self.assemble(line)
                    if bytecode:
                        self.current_address += len(bytecode)
                        self.current_instr_index += 1
        
        if not enable_relaxation:
            return self._generate_code(lines)
        
        max_iterations = 10
        for iteration in range(max_iterations):
            old_jump_forms = self.jump_forms.copy()
            
            self.current_address = 0
            self.current_instr_index = 0
            new_jump_forms = {}
            
            for line in lines:
                if not line or line.startswith(';'):
                    continue
                    
                if line.endswith(':'):
                    continue
                
                parts = re.split(r'[\s,]+', line.lower())
                operation = parts[0]
                
                is_jump = operation in ['jmp', 'je', 'jz', 'jne', 'jnz', 'jl', 'jle', 'jg', 'jge',
                                       'ja', 'jae', 'jb', 'jbe', 'jnge', 'jng', 'jnle', 'jnl',
                                       'jnbe', 'jnb', 'jnae', 'jna']
                
                if is_jump and len(parts) >= 2:
                    target = parts[1]
                    
                    if not self.is_immediate(target) and target in self.labels:
                        current_form = self.jump_forms.get(self.current_instr_index, 'near')
                        
                        if operation == 'jmp':
                            instr_len = 2 if current_form == 'short' else 5
                        else:
                            instr_len = 2 if current_form == 'short' else 6
                        
                        offset = self.labels[target] - (self.current_address + instr_len)
                        
                        if -128 <= offset <= 127:
                            new_jump_forms[self.current_instr_index] = 'short'
                        else:
                            new_jump_forms[self.current_instr_index] = 'near'
                
                bytecode = self.assemble(line)
                if bytecode:
                    self.current_address += len(bytecode)
                    self.current_instr_index += 1
            
            self.jump_forms = new_jump_forms
            
            self.current_address = 0
            self.current_instr_index = 0
            self.labels = {}
            
            for line in lines:
                if line and not line.startswith(';'):
                    if line.endswith(':'):
                        self.labels[line[:-1]] = self.current_address
                    else:
                        bytecode = self.assemble(line)
                        if bytecode:
                            self.current_address += len(bytecode)
                            self.current_instr_index += 1
            
            if self.jump_forms == old_jump_forms:
                break
        else:
            print(f"Warnung: Relaxation konvergierte nicht nach {max_iterations} Iterationen")
        
        return self._generate_code(lines)
    
    def _generate_code(self, lines):
        self.current_address = 0
        self.current_instr_index = 0
        machine_code = []
        
        for line_num, line in enumerate(lines, 1):
            if not line or line.startswith(';'):
                continue
            
            try:
                bytecode = self.assemble(line)
                if bytecode is not None:
                    machine_code.extend(bytecode)
                    self.current_address += len(bytecode)
                    self.current_instr_index += 1
                elif not line.endswith(':'):
                    print(f"Fehler Zeile {line_num}: Unbekannte Instruktion: {line}")
            except Exception as e:
                print(f"Fehler Zeile {line_num} '{line}': {e}")
        
        return machine_code
    
    def format_hex(self, bytecode):
        return ' '.join(f'{byte:02X}' for byte in bytecode)
    
    def write_binary(self, bytecode, filename):
        with open(filename, 'wb') as f:
            f.write(bytes(bytecode))


def main():
    asm = Assembler()
    
    print("=" * 60)
    print("Vollwertiger x86/x86-64 Assembler mit Relaxation")
    print("=" * 60)
    print()
    
    # Beispiel 1: Simple function
    print("Beispiel 1: Einfache Funktion")
    code1 = """
    mov eax, 1
    ret
    """
    bytecode1 = asm.assemble_code(code1)
    print(f"Code: {code1.strip()}")
    print(f"Hex:  {asm.format_hex(bytecode1)}")
    print()
    
    # Beispiel 2: Mit Labels und Sprüngen (zeigt Relaxation)
    print("Beispiel 2: Mit Labels und Sprüngen (Short Jump Optimization)")
    code2 = """
    start:
    xor eax, eax
    inc eax
    cmp eax, 10
    jne start
    ret
    """
    bytecode2 = asm.assemble_code(code2, enable_relaxation=True)
    print(f"Mit Relaxation: {asm.format_hex(bytecode2)} ({len(bytecode2)} bytes)")
    bytecode2_no_relax = asm.assemble_code(code2, enable_relaxation=False)
    print(f"Ohne Relaxation: {asm.format_hex(bytecode2_no_relax)} ({len(bytecode2_no_relax)} bytes)")
    print()
    
    # Beispiel 3: Forward Jump (zeigt Relaxation bei Vorwärtssprung)
    print("Beispiel 3: Forward Jump")
    code3 = """
    jmp end
    nop
    nop
    end:
    ret
    """
    bytecode3 = asm.assemble_code(code3, enable_relaxation=True)
    print(f"Mit Relaxation: {asm.format_hex(bytecode3)}")
    bytecode3_no_relax = asm.assemble_code(code3, enable_relaxation=False)
    print(f"Ohne Relaxation: {asm.format_hex(bytecode3_no_relax)}")
    print()
    
    # Beispiel 4: Komplexeres Beispiel mit mehreren Jumps
    print("Beispiel 4: Fibonacci-ähnliche Loop")
    code4 = """
    xor rax, rax
    mov rbx, 1
    loop_start:
    add rax, rbx
    mov rcx, rax
    cmp rcx, 100
    jl loop_start
    ret
    """
    bytecode4 = asm.assemble_code(code4, enable_relaxation=True)
    print(f"Mit Relaxation: {asm.format_hex(bytecode4)} ({len(bytecode4)} bytes)")
    print()
    
    # Beispiel 5: Zeigt warum Index-basiert wichtig ist
    print("Beispiel 5: Backward Jump (Index-basierte Relaxation)")
    code5 = """
    start:
    nop
    nop
    jmp start
    """
    bytecode5 = asm.assemble_code(code5, enable_relaxation=True)
    print(f"Mit Relaxation: {asm.format_hex(bytecode5)} ({len(bytecode5)} bytes)")
    print(f"Jump sollte short sein: EB FD (nicht E9 FB FF FF FF)")
    bytecode5_no_relax = asm.assemble_code(code5, enable_relaxation=False)
    print(f"Ohne Relaxation: {asm.format_hex(bytecode5_no_relax)} ({len(bytecode5_no_relax)} bytes)")
    print()
    
    # Interaktiver Modus
    print("=" * 60)
    print("Interaktiver Modus")
    print("=" * 60)
    print("Befehle:")
    print("  Einzelne Zeile eingeben für sofortige Assemblierung")
    print("  'multi' für mehrzeiligen Modus")
    print("  'save <datei>' um den letzten Code zu speichern")
    print("  'quit' zum Beenden")
    print()
    
    last_bytecode = []
    
    while True:
        try:
            line = input("> ").strip()
            
            if not line:
                continue
            
            if line.lower() == 'quit':
                print("Auf Wiedersehen!")
                break
            
            if line.lower() == 'multi':
                print("Mehrzeiliger Modus (leere Zeile zum Beenden):")
                lines = []
                while True:
                    ml = input("... ")
                    if not ml.strip():
                        break
                    lines.append(ml)
                code = '\n'.join(lines)
                bytecode = asm.assemble_code(code)
                if bytecode:
                    print(f"Hex:  {asm.format_hex(bytecode)}")
                    print(f"Größe: {len(bytecode)} Bytes")
                    last_bytecode = bytecode
                print()
                continue
            
            if line.lower().startswith('save '):
                filename = line[5:].strip()
                if last_bytecode:
                    asm.write_binary(last_bytecode, filename)
                    print(f"Gespeichert: {filename} ({len(last_bytecode)} Bytes)")
                else:
                    print("Kein Code zum Speichern vorhanden!")
                print()
                continue
            
            # Einzelne Zeile assemblieren
            try:
                bytecode = asm.assemble(line)
                if bytecode:
                    print(f"Hex:  {asm.format_hex(bytecode)}")
                    last_bytecode = bytecode
                else:
                    print("Fehler: Instruktion nicht erkannt oder ungültig!")
            except ValueError as e:
                print(f"Wert-Fehler: {e}")
            except Exception as e:
                print(f"Fehler: {e}")
            print()
            
        except KeyboardInterrupt:
            print("\nAuf Wiedersehen!")
            break
        except Exception as e:
            print(f"Unerwarteter Fehler: {e}")
            import traceback
            traceback.print_exc()
            print()

if __name__ == '__main__':
    main()
