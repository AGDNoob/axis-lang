import sys
import argparse
from pathlib import Path

from tokenization_engine import Lexer
from syntactic_analyzer import Parser
from semantic_analyzer import SemanticAnalyzer
from code_generator import CodeGenerator
from executable_format_generator import ELF64Writer, write_elf_executable
from interpreter import Interpreter

# assembler backend importieren
sys.path.insert(0, str(Path(__file__).parent))
from assembler import Assembler


class CompilationPipeline:
    def __init__(self, verbose: bool = False):
        self.verbose = verbose
    
    def log(self, msg: str):
        # logging nur wenn verbose mode an ist
        if self.verbose:
            print(f"[PIPELINE] {msg}")
    
    def run_script(self, ast) -> bool:
        """Run a script-mode program using the interpreter"""
        try:
            interpreter = Interpreter()
            exit_code = interpreter.run(ast)
            if self.verbose:
                print(f"[PIPELINE] Script finished with exit code {exit_code}")
            return True
        except Exception as e:
            print(f"Runtime error: {e}", file=sys.stderr)
            import traceback
            traceback.print_exc()
            return False
    
    def compile(self, source_code: str) -> tuple[bytes, bytes, list, dict, bool]:
        """
        Compile source to machine code.
        
        Returns:
            tuple: (machine_code, rodata, relocations, string_offsets, needs_bss)
        """
        # die ganze pipeline durchlaufen: source -> tokens -> ast -> asm -> bytes
        self.log("Phase 1: Tokenization...")
        lexer = Lexer(source_code)
        tokens = lexer.tokenize()
        self.log(f"  Generated {len(tokens)} tokens")
        
        self.log("Phase 2: Parsing...")
        parser = Parser(tokens)
        ast = parser.parse()
        self.log(f"  Parsed {len(ast.functions)} functions")
        
        self.log("Phase 3: Semantic Analysis...")
        analyzer = SemanticAnalyzer()
        analyzer.analyze(ast)
        self.log("  Type checking complete")
        
        self.log("Phase 4: Code Generation...")
        codegen = CodeGenerator()
        assembly = codegen.compile(ast)
        string_data = codegen.get_string_data()  # label -> (content, length)
        needs_bss = codegen.needs_bss_section()  # for read_failed flag
        
        if self.verbose:
            print("\n--- Generated Assembly ---")
            print(assembly)
            print("--- End Assembly ---\n")
            if string_data:
                print("--- String Data ---")
                for label, (content, length) in string_data.items():
                    print(f"  {label}: {repr(content)} ({length} bytes)")
                print("--- End String Data ---\n")
            if needs_bss:
                print("--- BSS Section ---")
                print("  _read_failed: 1 byte")
                print("--- End BSS ---\n")
        
        self.log("Phase 5: Assembling to machine code...")
        assembler = Assembler()
        machine_code = assembler.assemble_code(assembly)
        relocations = assembler.get_string_relocations()
        
        if not machine_code:
            raise RuntimeError("Assembly failed: No machine code generated")
        
        self.log(f"  Generated {len(machine_code)} bytes of machine code")
        
        # Build rodata section
        rodata = bytearray()
        string_offsets = {}  # label -> offset in rodata
        for label, (content, length) in string_data.items():
            string_offsets[label] = len(rodata)
            # Encode string content with escape sequences
            rodata.extend(content.encode('utf-8'))
        
        if rodata:
            self.log(f"  Generated {len(rodata)} bytes of string data")
            self.log(f"  {len(relocations)} relocations to patch")
        
        return bytes(machine_code), bytes(rodata), relocations, string_offsets, needs_bss
    
    def compile_file(self, input_path: str, output_path: str = None, dump_hex: bool = True, elf_format: bool = False):
        self.log(f"Reading source: {input_path}")
        with open(input_path, 'r', encoding='utf-8') as f:
            source_code = f.read()
        
        # First pass: detect mode
        self.log("Detecting mode...")
        lexer = Lexer(source_code)
        tokens = lexer.tokenize()
        parser = Parser(tokens)
        ast = parser.parse()
        
        if ast.mode == "script":
            # Script mode: interpret
            self.log("Script mode detected - running interpreter...")
            return self.run_script(ast)
        
        # Compile mode: continue with compilation
        self.log("Compile mode - generating binary...")
        
        # Compile
        try:
            machine_code, rodata, relocations, string_offsets, needs_bss = self.compile(source_code)
        except Exception as e:
            print(f"Compilation failed: {e}", file=sys.stderr)
            import traceback
            traceback.print_exc()
            return False
        
        # Hex dump
        if dump_hex:
            print("\n=== Machine Code (Hex) ===")
            hex_str = ' '.join(f'{byte:02X}' for byte in machine_code)
            # Print in rows of 16 bytes
            for i in range(0, len(hex_str), 48):  # 16*3 = 48
                print(hex_str[i:i+48])
            print(f"\nTotal: {len(machine_code)} bytes\n")
        
        # Write binary
        if output_path:
            self.log(f"Writing binary: {output_path}")
            
            if elf_format:
                # BSS size: 8 bytes for _read_failed (aligned)
                bss_size = 8 if needs_bss else 0
                
                # Generate ELF64 Executable with rodata and bss
                rodata_vaddr, bss_vaddr = write_elf_executable(
                    machine_code, output_path, 
                    rodata=rodata, 
                    string_offsets=string_offsets,
                    bss_size=bss_size,
                    verbose=self.verbose
                )
                
                # Patch relocations in the executable
                if relocations or needs_bss:
                    self.log(f"Patching relocations...")
                    self.patch_relocations(output_path, relocations, string_offsets, 
                                          rodata_vaddr, bss_vaddr if needs_bss else 0)
                
                print(f"ELF64 executable written to: {output_path}")
                if not self.verbose:
                    print(f"Run with: chmod +x {output_path} && ./{output_path}")
            else:
                with open(output_path, 'wb') as f:
                    f.write(machine_code)
                print(f"Binary written to: {output_path}")
        
        return True
    
    def patch_relocations(self, output_path: str, relocations: list, string_offsets: dict, 
                          rodata_vaddr: int, bss_vaddr: int = 0):
        """Patch string and BSS address placeholders in the ELF file"""
        import struct
        
        # Read the file
        with open(output_path, 'rb') as f:
            data = bytearray(f.read())
        
        # Code starts at offset 0x1000 (PAGE_SIZE), then _start stub (16 bytes)
        code_file_offset = 0x1000 + 16  # Skip headers + padding + _start stub
        
        # Patch string relocations
        for offset, label in relocations:
            if label == '_read_failed':
                # BSS relocation
                if bss_vaddr == 0:
                    print(f"Warning: _read_failed used but no BSS section")
                    continue
                addr = bss_vaddr  # _read_failed is at start of BSS
            elif label in string_offsets:
                # String relocation
                addr = rodata_vaddr + string_offsets[label]
            else:
                print(f"Warning: Unknown label '{label}'")
                continue
            
            # File offset to patch = code_file_offset + relocation offset
            patch_offset = code_file_offset + offset
            
            if self.verbose:
                print(f"  Patching {label} at offset 0x{patch_offset:X} -> 0x{addr:X}")
            
            # Write 64-bit address
            addr_bytes = struct.pack('<Q', addr)
            data[patch_offset:patch_offset+8] = addr_bytes
        
        # Write back
        with open(output_path, 'wb') as f:
            f.write(data)


def main():
    parser = argparse.ArgumentParser(
        description='AXIS - System Programming Language with Script and Compile modes',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Commands:
  run      Execute a script (mode script) - interpreted
  build    Compile to binary (mode compile) - native ELF

Examples:
  python compilation_pipeline.py run script.axis
  python compilation_pipeline.py build prog.axis -o prog
  python compilation_pipeline.py prog.axis          # auto-detect mode
        """
    )
    
    parser.add_argument('command', nargs='?', help='Command: run, build, or omit for auto-detect')
    parser.add_argument('input', nargs='?', help='Input source file (.axis)')
    parser.add_argument('-o', '--output', help='Output binary file (build mode only)')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')
    parser.add_argument('--no-hex', action='store_true', help='Disable hex dump')
    parser.add_argument('--elf', action='store_true', help='Generate ELF64 executable (Linux)')
    
    args = parser.parse_args()
    
    # Handle command parsing
    input_file = None
    force_run = False
    force_build = False
    
    if args.command in ['run', 'build']:
        # Explicit command
        if args.command == 'run':
            force_run = True
        else:
            force_build = True
        input_file = args.input
    elif args.command and args.command.endswith('.axis'):
        # No command, just file (auto-detect)
        input_file = args.command
    elif args.command:
        # Unknown command - treat as filename
        input_file = args.command
    
    if not input_file:
        parser.print_help()
        return 1
    
    if not Path(input_file).exists():
        print(f"Error: Input file not found: {input_file}", file=sys.stderr)
        return 1
    
    pipeline = CompilationPipeline(verbose=args.verbose)
    
    # Read file to detect mode
    with open(input_file, 'r', encoding='utf-8') as f:
        source_code = f.read()
    
    lexer = Lexer(source_code)
    tokens = lexer.tokenize()
    parser_obj = Parser(tokens)
    ast = parser_obj.parse()
    
    # Override mode if forced
    if force_run:
        if ast.mode == "compile":
            print("Warning: File uses 'mode compile' but running with 'run' command. Interpreting anyway.")
        return 0 if pipeline.run_script(ast) else 1
    
    if force_build or ast.mode == "compile":
        # Compile mode
        if not args.output:
            # Default output name: same directory as input, same name without .axis
            input_path = Path(input_file)
            output = str(input_path.parent / input_path.stem)
        else:
            output = args.output
        
        success = pipeline.compile_file(
            input_file,
            output_path=output,
            dump_hex=not args.no_hex,
            elf_format=args.elf or True  # Default to ELF
        )
        return 0 if success else 1
    else:
        # Script mode (auto-detected)
        return 0 if pipeline.run_script(ast) else 1


if __name__ == '__main__':
    # Quick test without args
    if len(sys.argv) == 1:
        print("=" * 70)
        print("AXIS Compilation Pipeline - Quick Test")
        print("=" * 70)
        print()
        
        # Test 1: Simple arithmetic
        print("Test 1: Simple arithmetic")
        print("-" * 70)
        test_source1 = """
fn main() -> i32 {
    let x: i32 = 10;
    let y: i32 = 20;
    return x + y;
}
        """
        
        pipeline = CompilationPipeline(verbose=True)
        try:
            machine_code = pipeline.compile(test_source1)
            print("\n✓ Compilation successful!")
            print(f"Machine code: {' '.join(f'{b:02X}' for b in machine_code[:32])}...")
        except Exception as e:
            print(f"\n✗ Compilation failed: {e}")
            import traceback
            traceback.print_exc()
        
        print("\n" + "=" * 70)
        print("For full usage, run:")
        print("  python compilation_pipeline.py <source.axis> [-o output.bin] [-v]")
        print("=" * 70)
    else:
        sys.exit(main())
