#!/bin/sh
export RUSTICL_ENABLE=cuda

threads=24
clinfo > rusticl.cluda.clinfo
../opencl_cts_runner/clctsrunner.py -w -c online --quick -j $threads > rusticl.online.cluda
#RUSTICL_ENABLE=iris CL_DEVICE_TYPE=CL_DEVICE_TYPE_GPU ../opencl_cts_runner/clctsrunner.py -w -c spir-v -j $threads > rusticl.spirv.iris.new
