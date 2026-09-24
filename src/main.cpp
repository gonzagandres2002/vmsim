#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "application/simulator.h"
#include "domain/fifo.h"
#include "domain/lru.h"
#include "domain/virtual_memory.h"
#include "infrastructure/program_file.h"

static const uint32_t DEFAULT_PHYSICAL_KB = 256;  // el mínimo que pide el enunciado
static const uint32_t DEFAULT_PAGE_KB = 4;

// Convierte un argumento a un tamaño en KB. Rechaza texto sobrante ("4x"), signos y ceros.
static uint32_t parse_kb(const std::string& text, const std::string& what) {
    try {
        size_t used = 0;
        unsigned long value = std::stoul(text, &used);
        if (used != text.size() || text[0] == '-' || text[0] == '+') throw std::invalid_argument("format");
        if (value == 0 || value > 4u * 1024 * 1024 - 1) throw std::out_of_range("range");  // cabe en 32 bits
        return (uint32_t)value;
    } catch (const std::exception&) {
        throw std::invalid_argument(what + " must be a positive integer in KB (got '" + text + "')");
    }
}

// Único lugar del programa que decide qué política concreta se usa.
static std::unique_ptr<ReplacementPolicy> make_policy(const std::string& name) {
    if (name == "fifo") return std::make_unique<Fifo>();
    if (name == "lru") return std::make_unique<Lru>();
    throw std::invalid_argument("unknown policy '" + name + "' (use fifo or lru)");
}

static std::string describe_access(const AccessResult& a, const AddressLayout& layout) {
    std::string text = "VPN " + std::to_string(a.vpn) + " (PT1 " + std::to_string(layout.level1_index(a.vpn)) +
                       ", PT2 " + std::to_string(layout.level2_index(a.vpn)) + ", offset " +
                       std::to_string(layout.offset(a.virtual_address)) + ") | ";
    if (!a.page_fault) {
        text += "HIT: frame " + std::to_string(a.frame);
    } else {
        text += "PAGE FAULT: ";
        if (a.level2_created) text += "level-2 table created, ";
        if (a.replaced) {
            text += "evicted VPN " + std::to_string(a.evicted_vpn) + " from frame " + std::to_string(a.frame);
            text += a.written_back ? " (dirty: written to disk), " : " (clean: discarded), ";
        } else {
            text += "free frame " + std::to_string(a.frame) + ", ";
        }
        text += a.loaded_from_disk ? "page loaded from disk" : "new page filled with zeros";
    }
    return text + " | PA " + hex_address(a.physical_address);
}

static void print_trace(const std::vector<TraceStep>& trace, const AddressLayout& layout) {
    for (const TraceStep& step : trace) {
        std::cout << "[" << step.op.line << "] ";
        switch (step.op.kind) {
            case OperationKind::Alloc: {
                uint32_t last = step.allocated.start + step.allocated.pages * layout.page_size - 1;
                std::cout << "alloc " << step.op.bytes << " -> region [" << hex_address(step.allocated.start) << " .. "
                          << hex_address(last) << "], " << step.allocated.pages << " page(s)";
                break;
            }
            case OperationKind::Write:
                std::cout << "write " << hex_address(step.op.address) << " <- " << (int)step.op.value << " | "
                          << describe_access(step.access, layout);
                break;
            case OperationKind::Read:
                std::cout << "read " << hex_address(step.op.address) << " -> " << (int)step.access.value << " | "
                          << describe_access(step.access, layout);
                break;
            case OperationKind::Free:
                std::cout << "free " << hex_address(step.op.address) << " -> " << step.freed.pages << " page(s), "
                          << step.freed.frames_released << " frame(s) released";
                break;
        }
        std::cout << "\n";
    }
}

// Las cinco primeras líneas son exactamente las que pide el enunciado.
static void print_stats(const Stats& s, const VirtualMemory& memory, const std::string& policy_name,
                        uint32_t physical_kb, uint32_t page_kb) {
    std::cout << "Total de accesos: " << s.accesses << "\n";
    std::cout << "Total fallos de página: " << s.page_faults << "\n";
    std::cout << "Hit rate: ";
    if (s.accesses == 0) std::cout << "n/a\n";
    else std::cout << std::fixed << std::setprecision(2) << s.hit_rate() << "%\n";
    std::cout << "Total reemplazos: " << s.replacements << "\n";
    std::cout << "Política: " << policy_name << "\n";
    std::cout << "Escrituras a disco (páginas sucias expulsadas): " << s.writebacks << "\n";
    std::cout << "Tablas de nivel 2 creadas: " << memory.page_table().level2_count() << "\n";
    std::cout << "Tiempo estimado: " << std::fixed << std::setprecision(4) << (double)s.estimated_time_ns() / 1e6
              << " ms (100 ns por acceso + 10 ms por fallo o escritura a disco)\n";
    std::cout << "Memoria física: " << physical_kb << " KB = " << memory.physical_memory().frame_count()
              << " marcos de " << page_kb << " KB\n";
}

// Uso: ./vmsim <program.txt> [fifo|lru] [physical_kb] [page_kb] [--trace]
int main(int argc, char* argv[]) {
    std::vector<std::string> positional;
    bool trace_enabled = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--trace") trace_enabled = true;
        else positional.push_back(argv[i]);
    }
    if (positional.empty() || positional.size() > 4) {
        std::cerr << "Usage: " << argv[0] << " <program.txt> [fifo|lru] [physical_kb] [page_kb] [--trace]\n";
        return 2;
    }
    try {
        std::string policy_name = positional.size() > 1 ? positional[1] : "fifo";
        uint32_t physical_kb = positional.size() > 2 ? parse_kb(positional[2], "physical memory") : DEFAULT_PHYSICAL_KB;
        uint32_t page_kb = positional.size() > 3 ? parse_kb(positional[3], "page size") : DEFAULT_PAGE_KB;
        if (physical_kb % page_kb != 0)
            throw std::invalid_argument("physical memory (" + std::to_string(physical_kb) +
                                        " KB) must be a multiple of the page size (" + std::to_string(page_kb) + " KB)");

        AddressLayout layout(page_kb * 1024);
        std::unique_ptr<ReplacementPolicy> policy = make_policy(policy_name);
        VirtualMemory memory(layout, physical_kb / page_kb, *policy);

        std::vector<Operation> program = read_program(positional[0]);
        SimulationResult result = run_program(program, memory);

        if (trace_enabled) {
            print_trace(result.trace, layout);
            std::cout << "\n";
        }
        print_stats(result.stats, memory, policy->name(), physical_kb, page_kb);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
