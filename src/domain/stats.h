#pragma once
#include <cstdint>

// Costos del modelo AMAT visto en clase (OSTEP cap. 22): T_M = 100 ns, T_D = 10 ms.
constexpr uint64_t MEMORY_ACCESS_NS = 100;
constexpr uint64_t DISK_ACCESS_NS = 10'000'000;

struct Stats {
    uint64_t accesses = 0;      // lecturas + escrituras
    uint64_t page_faults = 0;
    uint64_t replacements = 0;  // fallos en los que hubo que expulsar otra página
    uint64_t writebacks = 0;    // páginas sucias escritas a disco al expulsarlas

    uint64_t hits() const { return accesses - page_faults; }

    // (N - M) / N * 100. Sin accesos no está definido; se devuelve 0.
    double hit_rate() const { return accesses == 0 ? 0.0 : 100.0 * (double)hits() / (double)accesses; }

    // Cada acceso cuesta T_M; cada fallo (lectura de disco) y cada escritura a disco cuestan T_D.
    uint64_t estimated_time_ns() const {
        return accesses * MEMORY_ACCESS_NS + (page_faults + writebacks) * DISK_ACCESS_NS;
    }
};
