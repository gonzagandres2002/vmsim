#pragma once
#include <cstdint>
#include <map>
#include <vector>
#include "domain/address_layout.h"
#include "domain/page_table.h"
#include "domain/physical_memory.h"
#include "domain/replacement_policy.h"
#include "domain/stats.h"

// Todo lo que pasó en un acceso (read o write). Es la materia prima de la traza.
struct AccessResult {
    uint32_t virtual_address = 0;
    uint32_t vpn = 0;
    uint32_t frame = 0;
    uint32_t physical_address = 0;
    uint8_t value = 0;              // lo que se leyó o se escribió
    bool page_fault = false;
    bool level2_created = false;    // el acceso obligó a crear la tabla de nivel 2
    bool loaded_from_disk = false;  // la página venía del swap (ya había estado en memoria)
    bool replaced = false;          // no había marco libre: se expulsó otra página
    uint32_t evicted_vpn = 0;       // la página expulsada (solo si replaced)
    bool written_back = false;      // la expulsada estaba sucia y hubo que escribirla a disco
};

struct AllocResult {
    uint32_t start = 0;  // primera dirección de la región
    uint32_t pages = 0;
};

struct FreeResult {
    uint32_t pages = 0;
    uint32_t frames_released = 0;  // cuántas de esas páginas estaban en memoria física
};

// El espacio de direcciones de un proceso: reserva regiones, traduce direcciones,
// atiende los fallos de página y, cuando no hay marcos libres, aplica la política de reemplazo.
class VirtualMemory {
public:
    VirtualMemory(const AddressLayout& layout, uint32_t frame_count, ReplacementPolicy& policy);

    AllocResult alloc(uint32_t bytes);    // reserva páginas completas, una región tras otra
    FreeResult free(uint32_t address);    // libera la región que empieza en address
    AccessResult read(uint32_t address);
    AccessResult write(uint32_t address, uint8_t value);

    const Stats& stats() const { return counters; }
    const AddressLayout& address_layout() const { return layout; }
    const PageTable& page_table() const { return table; }
    const PhysicalMemory& physical_memory() const { return memory; }

private:
    AddressLayout layout;
    PageTable table;
    PhysicalMemory memory;
    ReplacementPolicy& policy;
    std::map<uint32_t, uint64_t> regions;           // inicio -> tamaño en bytes de cada alloc vivo
    uint64_t next_address = 0;                      // el espacio virtual se reparte en orden
    std::vector<uint32_t> frame_owner;              // marco -> VPN de la página que contiene
    std::map<uint32_t, std::vector<uint8_t>> swap;  // VPN -> contenido guardado en disco
    Stats counters;

    void require_allocated(uint32_t address) const;
    AccessResult translate(uint32_t address, bool is_write);
    void handle_page_fault(uint32_t vpn, PageTableEntry& pte, AccessResult& result);
    uint32_t evict_page(AccessResult& result);
};
