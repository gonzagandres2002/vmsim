#pragma once
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

// El espacio virtual es de 32 bits (requisito del enunciado).
constexpr int VIRTUAL_ADDRESS_BITS = 32;

// Cómo se parte una dirección virtual según el tamaño de página.
// Con páginas de 4 KB (el valor por defecto) queda exactamente como pide el enunciado:
//
//    31           22 21           12 11            0
//   ┌───────────────┬───────────────┬───────────────┐
//   │  PT1 (10 b)   │  PT2 (10 b)   │ offset (12 b) │
//   └───────────────┴───────────────┴───────────────┘
//
// offset_bits = log2(tamaño de página). Los 32 - offset_bits restantes son el VPN y se
// reparten entre los dos niveles; si son impares, el nivel 1 se queda con el bit extra.
struct AddressLayout {
    uint32_t page_size;  // bytes
    int offset_bits;
    int level2_bits;     // PT2: índice dentro de una tabla de nivel 2
    int level1_bits;     // PT1: índice en el directorio (nivel 1)

    explicit AddressLayout(uint32_t page_size_bytes) : page_size(page_size_bytes) {
        if (page_size == 0 || (page_size & (page_size - 1)) != 0)
            throw std::invalid_argument("page size must be a power of two");
        offset_bits = 0;
        while ((1u << offset_bits) < page_size) offset_bits++;
        int vpn_bits = VIRTUAL_ADDRESS_BITS - offset_bits;
        if (vpn_bits < 2) throw std::invalid_argument("page size too large for a two-level page table");
        level2_bits = vpn_bits / 2;
        level1_bits = vpn_bits - level2_bits;
    }

    uint32_t offset(uint32_t virtual_address) const { return virtual_address & (page_size - 1); }
    uint32_t vpn(uint32_t virtual_address) const { return virtual_address >> offset_bits; }

    // Los índices se calculan a partir del VPN (la parte alta de la dirección).
    uint32_t level1_index(uint32_t vpn) const { return vpn >> level2_bits; }
    uint32_t level2_index(uint32_t vpn) const { return vpn & ((1u << level2_bits) - 1); }
    uint32_t level1_entries() const { return 1u << level1_bits; }
    uint32_t level2_entries() const { return 1u << level2_bits; }

    uint32_t physical_address(uint32_t frame, uint32_t offset) const { return (frame << offset_bits) | offset; }
};

// "0x00001000": las direcciones se muestran siempre en hexadecimal con 8 dígitos.
inline std::string hex_address(uint32_t address) {
    char text[11];
    std::snprintf(text, sizeof text, "0x%08x", (unsigned)address);
    return text;
}
