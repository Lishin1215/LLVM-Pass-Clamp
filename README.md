# LLVM Image Optimization Pass

An LLVM compiler optimization pass specifically designed to optimize clamp operations in image processing libraries.

## Overview

This LLVM Pass identifies and optimizes common value clamping operations in image processing code through LLVM IR-level transformations to improve execution performance.

Supported image processing libraries:
- **Filament** - Google's real-time physically based rendering engine
- **libpng** - PNG image format processing library
- **stb** - Single-file image processing library

## Requirements

- LLVM 18
- CMake 3.15+
- Clang/Clang++
- Python 3 (for instruction counting analysis)
- perf (for performance evaluation)

## Building the Project

### 1. Compile LLVM Pass

```bash
# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake ..

# Build
make

# This will generate ImgOptPass.so
```

### 2. Setup Test Environment

Download and setup the required image processing libraries for testing:

```bash
# Enter demo directory
cd demo

# Download Filament
git clone https://github.com/google/filament.git
# Copy examples/filament_example.cpp to filament/ directory as test program

# Download libpng
git clone https://github.com/glennrp/libpng.git
# Copy examples/libpng_example.c to libpng/ directory as test program

# Download stb
git clone https://github.com/nothings/stb.git
# Copy examples/stb_example.c to stb/ directory as test program
```

## Usage

### Basic Usage

Apply this LLVM Pass during compilation:

```bash
# Generate LLVM IR
clang -S -emit-llvm -O3 your_code.c -o your_code.ll

# Apply optimization pass
opt -load-pass-plugin=./build/ImgOptPass.so -passes="img-opt" \
    your_code.ll -S -o your_code_optimized.ll

# Compile optimized code
clang -O3 your_code_optimized.ll -o your_code_optimized
```

### Running Tests

The project provides comprehensive test scripts to evaluate optimization effectiveness:

```bash
cd build_demo

# Run all tests (includes stb, filament, libpng)
bash test_all.sh
```

This script will:
1. Compile each test program (before/after optimization)
2. Measure execution performance using `perf`
3. Analyze IR instruction count differences using Python script

### Individual Tests

```bash
cd build_demo

# Test STB
cd build_stb
bash stb_application_compile.sh
./llvm_pass_application_o3              # Unoptimized version
./llvm_pass_application_o3_mypass_o3    # Optimized version

# Test Filament
cd build_filament
bash filament_application_compile.sh
./llvm_pass_application_o3              # Unoptimized version
./llvm_pass_application_o3_mypass_o3    # Optimized version

# Test libpng
cd build_libpng
bash libpng_application_compile.sh
./llvm_pass_application_o3              # Unoptimized version
./llvm_pass_application_o3_mypass_o3    # Optimized version
```

## Project Structure

```
.
├── ImgOptPass.cpp           # LLVM Pass main source
├── CMakeLists.txt           # CMake configuration
├── README.md                # This file
├── build/                   # Build output directory
│   └── ImgOptPass.so        # Compiled pass module
├── build_demo/              # Testing and evaluation
│   ├── test_all.sh          # Run all tests
│   ├── count_instructions_final.py  # IR instruction counting tool
│   ├── build_stb/           # STB test compilation scripts
│   ├── build_filament/      # Filament test compilation scripts
│   └── build_libpng/        # libpng test compilation scripts
├── demo/                    # Image processing libraries (download separately)
│   ├── filament/
│   ├── libpng/
│   └── stb/
└── examples/                # Example test programs
    ├── filament_example.cpp
    ├── libpng_example.c
    └── stb_example.c
```

## Performance Evaluation

Use the provided Python script to analyze optimization effectiveness:

```bash
cd build_demo

# Analyze IR instruction count
python3 count_instructions_final.py <ir_file.ll>
```
