// Disassemble DXBC shader bytecode using D3DDisassemble
// Build: cl /EHsc disasm_dxbc.cpp /link d3dcompiler.lib
// Usage: disasm_dxbc.exe shader.dxbc > shader.asm

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <shader.dxbc> [output.asm]\n", argv[0]);
        return 1;
    }

    // Read DXBC file
    FILE* f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "Cannot open: %s\n", argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(size);
    fread(data.data(), 1, size, f);
    fclose(f);

    // Disassemble
    ID3DBlob* disasm = nullptr;
    HRESULT hr = D3DDisassemble(data.data(), data.size(),
        D3D_DISASM_ENABLE_DEFAULT_VALUE_PRINTS | D3D_DISASM_ENABLE_INSTRUCTION_NUMBERING,
        nullptr, &disasm);

    if (FAILED(hr)) {
        fprintf(stderr, "D3DDisassemble failed: 0x%08X\n", hr);
        return 1;
    }

    // Output
    FILE* out = stdout;
    if (argc >= 3) {
        out = fopen(argv[2], "w");
        if (!out) {
            fprintf(stderr, "Cannot write: %s\n", argv[2]);
            disasm->Release();
            return 1;
        }
    }

    fwrite(disasm->GetBufferPointer(), 1, disasm->GetBufferSize(), out);

    if (out != stdout) fclose(out);
    disasm->Release();
    return 0;
}
