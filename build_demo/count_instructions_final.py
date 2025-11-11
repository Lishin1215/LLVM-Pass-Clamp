#!/usr/bin/env python3
import sys
import re

def count_instructions(filename):
    """只統計 LLVM IR 中實際的執行指令"""
    in_function = False
    count = 0

    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()

            # 跳過空行與註解
            if not line or line.startswith(';'):
                continue

            # 函數開始與結束
            if line.startswith('define '):
                in_function = True
                continue
            if line == '}' and in_function:
                in_function = False
                continue

            # 只統計函數內的內容
            if not in_function:
                continue

            # 跳過非指令行（label、metadata、屬性）
            if (line.endswith(':') or line.startswith('!') or
                line.startswith('attributes') or line.startswith('declare')):
                continue

            # 若符合 IR 指令格式：%x = inst ... 或 inst ...
            if re.match(r'^(%[\w\.]+ = )?\w+', line):
                count += 1

    return count


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <file.ll>")
        sys.exit(1)

    total = count_instructions(sys.argv[1])
    print(total)
