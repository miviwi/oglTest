#pragma once

#include "vm/vm.h"
#include "vm/assembler.h"

#include <vector>

namespace glang {

__declspec(dllexport) assembler::CodeObject compile_string(const char *src, bool output_assembly = false);

/*
  BINARY FORMAT:
    GLNG [4 byte size (little endian)] [program]
*/

__declspec(dllexport) std::vector<unsigned char> export_binary(assembler::CodeObject& co);
__declspec(dllexport) assembler::CodeObject import_binary(unsigned char *binary);

}