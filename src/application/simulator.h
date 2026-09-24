#pragma once
#include <vector>
#include "domain/operation.h"
#include "domain/virtual_memory.h"

// Lo que pasó al ejecutar una instrucción. Solo se llena la parte que corresponde a su tipo.
struct TraceStep {
    Operation op;
    AllocResult allocated;  // alloc
    AccessResult access;    // read y write
    FreeResult freed;       // free
};

struct SimulationResult {
    std::vector<TraceStep> trace;
    Stats stats;
};

// Ejecuta el programa instrucción por instrucción sobre la memoria virtual.
// Un error en una instrucción (por ejemplo, un segmentation fault) aborta la
// simulación con un mensaje que indica la línea del archivo.
SimulationResult run_program(const std::vector<Operation>& program, VirtualMemory& memory);
