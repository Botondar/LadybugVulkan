@echo off

set out_path=%1
set version=460core

IF NOT EXIST %out_path%Shaders (
    mkdir %out_path%Shaders
) ELSE (
    del %out_path%Shaders\*.spv
)

glslc ./src/Shaders/shader.vert -o %out_path%Shaders/vert.spv -std=%version%
glslc ./src/Shaders/shader.frag -o %out_path%Shaders/frag.spv -std=%version%

spirv-link %out_path%Shaders/vert.spv %out_path%Shaders/frag.spv -o %out_path%Shaders/shader.spv

del %out_path%Shaders\vert.spv
del %out_path%Shaders\frag.spv