#!/usr/bin/env bash
set -e
SRC=/home/ubuntu/llvm-pass/demo/filament/application.cpp
COLOR_CPP=/home/ubuntu/llvm-pass/demo/filament/filament/src/Color.cpp
COLORSPACE_CPP=/home/ubuntu/llvm-pass/demo/filament/filament/src/ColorSpaceUtils.cpp

# Include paths (no space between -I and path to avoid parsing issues)
FILAMENT_INCLUDE="-I/home/ubuntu/llvm-pass/demo/filament/filament/include \
-I/home/ubuntu/llvm-pass/demo/filament/filament/src \
-I/home/ubuntu/llvm-pass/demo/filament/libs/image/include \
-I/home/ubuntu/llvm-pass/demo/filament/libs/math/include \
-I/home/ubuntu/llvm-pass/demo/filament/libs/utils/include"

# 直接用 clang++ (會自動連結 libstdc++) - compile to native executable
clang++-18 -O3 $SRC $COLOR_CPP $COLORSPACE_CPP -o llvm_pass_application_o3 $FILAMENT_INCLUDE -lm

# 產 IR：各 TU 直接輸出 .ll（少一步 llvm-dis）
clang++-18 -O3 -S -emit-llvm $SRC -o application.ll $FILAMENT_INCLUDE
clang++-18 -O3 -S -emit-llvm $COLOR_CPP -o Color.ll $FILAMENT_INCLUDE
clang++-18 -O3 -S -emit-llvm $COLORSPACE_CPP -o ColorSpaceUtils.ll $FILAMENT_INCLUDE

# Link 所有 IR 成單一模組
llvm-link-18 -S application.ll Color.ll ColorSpaceUtils.ll -o llvm_pass_application_o3.ll

# 跑 pass on the combined IR
opt-18 -load-pass-plugin="/home/ubuntu/llvm-pass/build/ImgOptPass.so" \
  -passes="function(img-opt)" -S llvm_pass_application_o3.ll \
  -o llvm_pass_application_o3_mypass.ll

# 再編譯優化後 IR
clang++-18 -O3 llvm_pass_application_o3_mypass.ll -o llvm_pass_application_o3_mypass_o3 $FILAMENT_INCLUDE -lm
clang++-18 -O3 -S -emit-llvm llvm_pass_application_o3_mypass.ll -o llvm_pass_application_o3_mypass_o3.ll $FILAMENT_INCLUDE -lm
