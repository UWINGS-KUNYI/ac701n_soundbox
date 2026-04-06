#!/usr/bin/env python3
"""
jl_strip.py - 清理 JieLi SDK 經 clang -E -P 生成的文字檔
模擬 strip-ini 的行為，適用於：
  .sh   - shell 後處理腳本 (download.sh)
  .ini  - 燒錄配置檔 (isd_config.ini)
  .ld   - linker script (sdk.ld)
  .used - SDK 功能清單 (sdk_used_list.used)

用法:
    python3 jl_strip.py <file>            # 直接覆蓋原檔
    python3 jl_strip.py <file> <output>   # 輸出到新檔
    python3 jl_strip.py *.sh *.ini *.ld   # 批次處理多個檔案
"""

import sys
import os
import glob


def strip_file(text, filename):
    lines = text.splitlines()
    result = []
    ext = os.path.splitext(filename)[1].lower()
    is_sh = ext == '.sh'
    is_ld = ext == '.ld'

    for line in lines:
        # 移除行首空白
        stripped = line.lstrip()

        # 跳過空白行
        if not stripped:
            continue

        if is_ld:
            # linker script：// 不合法，轉成 /* */ 以防萬一
            if stripped.startswith('//'):
                stripped = '/* ' + stripped[2:].strip() + ' */'
        else:
            # .sh / .ini / .used：直接跳過 // 殘留註解
            if stripped.startswith('//'):
                continue

        result.append(stripped)

    # .sh：確保 shebang 在第一行
    if is_sh:
        shebang_idx = next(
            (i for i, l in enumerate(result) if l.startswith('#!')), None
        )
        if shebang_idx is not None and shebang_idx != 0:
            shebang = result.pop(shebang_idx)
            result.insert(0, shebang)

    return '\n'.join(result) + '\n'


def process(src, dst=None):
    if dst is None:
        dst = src

    with open(src, 'r', encoding='utf-8', errors='replace') as f:
        text = f.read()

    cleaned = strip_file(text, os.path.basename(src))

    with open(dst, 'w', encoding='utf-8') as f:
        f.write(cleaned)

    label = f"{src} -> {dst}" if dst != src else src
    print(f"stripped: {label}")


if __name__ == '__main__':
    args = sys.argv[1:]

    if not args:
        print(__doc__)
        sys.exit(1)

    # 單檔帶輸出路徑模式：jl_strip.py src dst
    if len(args) == 2 and '*' not in args[0] and '*' not in args[1]:
        process(args[0], args[1])
    else:
        # 批次模式
        files = []
        for pattern in args:
            matched = glob.glob(pattern)
            files.extend(matched if matched else [pattern])
        for f in files:
            process(f)
