#!/usr/bin/env python3
"""Fix main_window.py structure: Move DecisionDetailDialog to end of file"""

import sys
from pathlib import Path

file_path = Path(r"d:\AAA_C\compression-tool\src\gui\widgets\main_window.py")

# Read the entire file
with open(file_path, 'r', encoding='utf-8') as f:
    lines = f.readlines()

print(f"Total lines: {len(lines)}")

# Find key line numbers (0-indexed)
main_window_end = None  # Line after _run_network_sim's last except block (line 1951, index 1950)
dialog_start = None     # "class DecisionDetailDialog" (line 1952, index 1951)
dialog_end = None       # Line before _on_webpage_heatmap (line 2068, index 2067)
on_webpage_start = None # "def _on_webpage_heatmap" (line 2069, index 2068)

for i, line in enumerate(lines):
    if 'QMessageBox.warning(self, "网络模拟错误", f"生成网络传输模拟失败:\\n{e}")' in line and main_window_end is None:
        main_window_end = i + 1  # This is the last line of MainWindow before DecisionDetailDialog
        print(f"MainWindow ends at line {main_window_end + 1} (index {main_window_end})")
    
    if 'class DecisionDetailDialog(QDialog):' in line:
        dialog_start = i
        print(f"DecisionDetailDialog starts at line {i + 1} (index {i})")
    
    # Look for the first method definition after DecisionDetailDialog that should be in MainWindow
    if dialog_start is not None and on_webpage_start is None:
        if line.strip().startswith('def _on_webpage_heatmap'):
            on_webpage_start = i
            dialog_end = i  # The line before this method
            print(f"_on_webpage_heatmap starts at line {i + 1} (index {i})")
            print(f"DecisionDetailDialog ends at line {i} (index {i - 1})")

if None in [main_window_end, dialog_start, dialog_end, on_webpage_start]:
    print("ERROR: Could not find all required markers!")
    print(f"  main_window_end: {main_window_end}")
    print(f"  dialog_start: {dialog_start}")
    print(f"  dialog_end: {dialog_end}")
    print(f"  on_webpage_start: {on_webpage_start}")
    sys.exit(1)

# Extract three parts
part_a = lines[:main_window_end]  # MainWindow class (lines 1-1951)
part_b = lines[dialog_start:dialog_end]  # DecisionDetailDialog class (lines 1952-2068)
part_c = lines[on_webpage_start:]  # Methods that should be in MainWindow (lines 2069-end)

print(f"\nPart A (MainWindow): {len(part_a)} lines (indices 0-{main_window_end-1})")
print(f"Part B (DecisionDetailDialog): {len(part_b)} lines (indices {dialog_start}-{dialog_end-1})")
print(f"Part C (MainWindow methods): {len(part_c)} lines (indices {on_webpage_start}-{len(lines)-1})")

# Reconstruct: Part A + Part C + empty line + Part B
new_lines = part_a + ['\n'] + part_c + ['\n', '\n'] + part_b

# Write back
with open(file_path, 'w', encoding='utf-8') as f:
    f.writelines(new_lines)

print(f"\n✅ File fixed! New total lines: {len(new_lines)}")
print(f"Structure: MainWindow (lines 1-{len(part_a) + len(part_c)}) + DecisionDetailDialog (lines {len(part_a) + len(part_c) + 3}-end)")
