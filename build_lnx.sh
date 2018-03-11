#!/bin/sh

mkdir -p build_lnx
cd build_lnx
cmake -g "Unix Makefiles" ../ -DCL_INCLUDE_DIR=<TO_SET> -DCL_LIB_DIR=<TO_SET> -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=. -DOPENCL_LIBRARIES=OpenCL
make --jobs 8
