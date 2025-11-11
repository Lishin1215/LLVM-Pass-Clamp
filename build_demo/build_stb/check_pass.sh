#!/bin/bash
echo "=== 檢查 Pass 是否有執行優化 ==="
echo ""
echo "原始 IR (O3):"
grep -E "select|fcmp|fmul.*0x3f7fffff|min|max" llvm_pass_application_o3.ll | head -10

echo ""
echo "Pass 後 IR:"
grep -E "select|fcmp|fmul.*0x3f7fffff|min|max" llvm_pass_application_o3_mypass.ll | head -10
