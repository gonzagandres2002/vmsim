#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "domain/address_layout.h"

// Entrada de la tabla de páginas (PTE): dónde está la página y sus bits de estado.
struct PageTableEntry {
    uint32_t frame = 0;     // número de marco físico (solo tiene sentido si valid)
    bool valid = false;     // la página está en memoria física
    bool accessed = false;  // se leyó o escribió desde que se cargó
    bool dirty = false;     // se escribió desde que se cargó: al expulsarla hay que guardarla en disco
};

// Tabla de páginas de dos niveles. El nivel 1 (directorio) tiene una entrada por cada
// tabla de nivel 2 posible; cada tabla de nivel 2 se crea la primera vez que se necesita.
class PageTable {
public:
    explicit PageTable(const AddressLayout& layout);

    bool has_level2(uint32_t vpn) const;
    // Entrada de la página, creando la tabla de nivel 2 si todavía no existe.
    PageTableEntry& entry(uint32_t vpn);
    // Entrada de la página sin crear nada: nullptr si su tabla de nivel 2 no existe.
    const PageTableEntry* find(uint32_t vpn) const;
    uint32_t level2_count() const { return created; }

private:
    using Level2Table = std::vector<PageTableEntry>;

    AddressLayout layout;
    std::vector<std::unique_ptr<Level2Table>> directory;  // nivel 1: nullptr = tabla no creada
    uint32_t created = 0;
};
