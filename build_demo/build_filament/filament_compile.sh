#!/usr/bin/env bash
set -e
SRC=/home/ubuntu/llvm-pass/demo/filament/llvm_pass_test_2.cpp

# Filament include paths (adjust based on your setup)
FILAMENT_INCLUDE="-I /home/ubuntu/llvm-pass/demo/filament/libs/image/include -I /home/ubuntu/llvm-pass/demo/filament/libs/math/include -I /home/ubuntu/llvm-pass/demo/filament/libs/utils/include"

# O3 
clang-18 -O3 $SRC -o llvm_pass_test_o3 -lm $FILAMENT_INCLUDE

# O3 + opt2
clang-18 -O3 -S -emit-llvm $SRC -o llvm_pass_test_o3.ll $FILAMENT_INCLUDE
opt-18 -load-pass-plugin="/home/ubuntu/llvm-pass/build/ImgOptPass.so" -passes="function(img-opt)" -S llvm_pass_test_o3.ll -o llvm_pass_test_o3_mypass_opt2.ll
clang-18 -O3 llvm_pass_test_o3_mypass_opt2.ll -o llvm_pass_test_o3_mypass_opt2 -lm