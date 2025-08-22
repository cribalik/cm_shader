#!/bin/bash
g++ cli.c -static -I/usr/include/vulkan -o shad -O2 -lm -lglslang -lSPIRV-Tools-opt -lSPIRV-Tools -lglslang-default-resource-limits -lpthread