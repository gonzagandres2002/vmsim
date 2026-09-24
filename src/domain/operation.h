#pragma once
#include <cstdint>

enum class OperationKind { Alloc, Write, Read, Free };

// Una instrucción del programa simulado (una línea del archivo de entrada).
struct Operation {
    OperationKind kind = OperationKind::Read;
    uint32_t bytes = 0;    // alloc
    uint32_t address = 0;  // write, read, free
    uint8_t value = 0;     // write
    int line = 0;          // línea del archivo, para la traza y los mensajes de error
};
