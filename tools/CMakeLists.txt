cmake_minimum_required(VERSION 3.20)
project(disasm_dxbc)
add_executable(disasm_dxbc disasm_dxbc.cpp)
target_link_libraries(disasm_dxbc d3dcompiler.lib)
