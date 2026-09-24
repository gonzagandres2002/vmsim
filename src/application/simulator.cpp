#include "application/simulator.h"
#include <stdexcept>
#include <string>

static TraceStep execute(const Operation& op, VirtualMemory& memory) {
    TraceStep step;
    step.op = op;
    switch (op.kind) {
        case OperationKind::Alloc: step.allocated = memory.alloc(op.bytes); break;
        case OperationKind::Write: step.access = memory.write(op.address, op.value); break;
        case OperationKind::Read: step.access = memory.read(op.address); break;
        case OperationKind::Free: step.freed = memory.free(op.address); break;
    }
    return step;
}

SimulationResult run_program(const std::vector<Operation>& program, VirtualMemory& memory) {
    SimulationResult result;
    for (const Operation& op : program) {
        try {
            result.trace.push_back(execute(op, memory));
        } catch (const std::exception& e) {
            throw std::runtime_error("line " + std::to_string(op.line) + ": " + e.what());
        }
    }
    result.stats = memory.stats();
    return result;
}
