#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Memory usage report generator for STM32 projects
"""
import sys
import os
import subprocess
import re
from pathlib import Path

# STM32F411CE memory limits (adjust if using different MCU)
FLASH_SIZE = 512 * 1024  # 512KB
RAM_SIZE = 128 * 1024     # 128KB

def run_command(cmd):
    """Run shell command and return output"""
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        return result.stdout
    except subprocess.CalledProcessError as e:
        print(f"Error running {' '.join(cmd)}: {e}", file=sys.stderr)
        return ""

def parse_size_output(output):
    """Parse arm-none-eabi-size berkeley format output"""
    lines = output.strip().split('\n')
    if len(lines) < 2:
        return None

    # Parse header and data line
    # Format: text    data     bss     dec     hex filename
    data_line = lines[1].split()
    if len(data_line) < 3:
        return None

    return {
        'text': int(data_line[0]),
        'data': int(data_line[1]),
        'bss': int(data_line[2]),
        'total': int(data_line[3]) if len(data_line) > 3 else 0
    }

def format_bytes(bytes_val):
    """Format bytes with units"""
    if bytes_val < 1024:
        return f"{bytes_val} B"
    elif bytes_val < 1024 * 1024:
        return f"{bytes_val / 1024:.2f} KB"
    else:
        return f"{bytes_val / (1024 * 1024):.2f} MB"

def format_percentage(used, total):
    """Format percentage with color"""
    pct = (used / total) * 100
    bar_width = 30
    filled = int(bar_width * used / total)
    bar = '█' * filled + '░' * (bar_width - filled)

    return f"{pct:5.1f}% [{bar}]"

def main():
    # Configure stdout for UTF-8 on Windows
    if sys.platform == 'win32':
        import io
        sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

    if len(sys.argv) < 3:
        print("Usage: memory_report.py <size_tool> <elf_file>", file=sys.stderr)
        sys.exit(1)

    size_tool = sys.argv[1]
    elf_file = sys.argv[2]

    if not Path(elf_file).exists():
        print(f"Error: ELF file not found: {elf_file}", file=sys.stderr)
        sys.exit(1)

    # Get size information
    size_output = run_command([size_tool, "--format=berkeley", elf_file])
    if not size_output:
        sys.exit(1)

    sizes = parse_size_output(size_output)
    if not sizes:
        print("Error: Could not parse size output", file=sys.stderr)
        sys.exit(1)

    # Calculate memory usage
    flash_used = sizes['text'] + sizes['data']
    ram_used = sizes['data'] + sizes['bss']

    # Print report
    print("\n" + "="*70)
    print("                      MEMORY USAGE REPORT")
    print("="*70)
    print(f"\n{'Section':<15} {'Size':<15} {'Description':<30}")
    print("-"*70)
    print(f"{'Text (code)':<15} {format_bytes(sizes['text']):<15} Program code in Flash")
    print(f"{'Data (init)':<15} {format_bytes(sizes['data']):<15} Initialized data (Flash->RAM)")
    print(f"{'BSS (uninit)':<15} {format_bytes(sizes['bss']):<15} Uninitialized data (RAM)")
    print("-"*70)
    print(f"{'Total':<15} {format_bytes(sizes['total']):<15} text + data + bss")

    print("\n" + "="*70)
    print("                     MEMORY ALLOCATION")
    print("="*70)

    # Flash usage
    print(f"\n[FLASH] {format_bytes(flash_used)} / {format_bytes(FLASH_SIZE)}")
    print(f"   {format_percentage(flash_used, FLASH_SIZE)}")
    print(f"   Free: {format_bytes(FLASH_SIZE - flash_used)}")

    # RAM usage
    print(f"\n[RAM]   {format_bytes(ram_used)} / {format_bytes(RAM_SIZE)}")
    print(f"   {format_percentage(ram_used, RAM_SIZE)}")
    print(f"   Free: {format_bytes(RAM_SIZE - ram_used)}")

    # Warnings
    print("\n" + "="*70)
    if flash_used > FLASH_SIZE * 0.9:
        print("[WARNING] Flash usage > 90%!")
    if ram_used > RAM_SIZE * 0.9:
        print("[WARNING] RAM usage > 90%!")
    if flash_used > FLASH_SIZE:
        print("[ERROR] Flash overflow!")
    if ram_used > RAM_SIZE:
        print("[ERROR] RAM overflow!")

    if flash_used <= FLASH_SIZE * 0.9 and ram_used <= RAM_SIZE * 0.9:
        print("[OK] Memory usage is healthy")

    print("="*70 + "\n")

if __name__ == "__main__":
    main()