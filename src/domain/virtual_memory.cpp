#include "domain/virtual_memory.h"
#include <stdexcept>

VirtualMemory::VirtualMemory(const AddressLayout& l, uint32_t frame_count, ReplacementPolicy& p)
    : layout(l), table(l), memory(frame_count, l.page_size), policy(p), frame_owner(frame_count) {}

AllocResult VirtualMemory::alloc(uint32_t bytes) {
    if (bytes == 0) throw std::invalid_argument("alloc: the size must be greater than 0");

    // Se reservan páginas completas: 8192 bytes son 2 páginas de 4 KB, 8193 son 3.
    uint64_t pages = ((uint64_t)bytes + layout.page_size - 1) / layout.page_size;
    uint64_t size = pages * layout.page_size;
    if (next_address + size > (1ull << VIRTUAL_ADDRESS_BITS))
        throw std::runtime_error("alloc: the 32-bit virtual address space is exhausted");

    AllocResult result;
    result.start = (uint32_t)next_address;
    result.pages = (uint32_t)pages;
    regions[result.start] = size;
    next_address += size;
    return result;
}

FreeResult VirtualMemory::free(uint32_t address) {
    auto region = regions.find(address);
    if (region == regions.end())
        throw std::runtime_error("free: " + hex_address(address) + " is not the start of an allocated region");

    FreeResult result;
    result.pages = (uint32_t)(region->second / layout.page_size);
    uint32_t first_vpn = layout.vpn(address);
    for (uint32_t vpn = first_vpn; vpn < first_vpn + result.pages; vpn++) {
        swap.erase(vpn);
        if (!table.has_level2(vpn)) continue;  // nunca se accedió: no hay entrada que limpiar
        PageTableEntry& pte = table.entry(vpn);
        if (pte.valid) {
            policy.on_release(pte.frame);
            memory.release_frame(pte.frame);
            result.frames_released++;
        }
        pte = PageTableEntry{};  // vuelve a "no válida", con los bits en cero
    }
    regions.erase(region);
    return result;
}

AccessResult VirtualMemory::read(uint32_t address) {
    AccessResult result = translate(address, false);
    result.value = memory.read(result.physical_address);
    return result;
}

AccessResult VirtualMemory::write(uint32_t address, uint8_t value) {
    AccessResult result = translate(address, true);
    memory.write(result.physical_address, value);
    result.value = value;
    return result;
}

void VirtualMemory::require_allocated(uint32_t address) const {
    // La región que contiene a address es la última que empieza en o antes de address.
    auto region = regions.upper_bound(address);
    bool inside = region != regions.begin() && address < (--region)->first + region->second;
    if (!inside)
        throw std::runtime_error("segmentation fault: " + hex_address(address) + " is not inside any allocated region");
}

// Traducción VA -> PA. Si la página no está en memoria, atiende el fallo antes de terminar.
AccessResult VirtualMemory::translate(uint32_t address, bool is_write) {
    require_allocated(address);
    counters.accesses++;

    AccessResult result;
    result.virtual_address = address;
    result.vpn = layout.vpn(address);
    result.level2_created = !table.has_level2(result.vpn);
    PageTableEntry& pte = table.entry(result.vpn);  // crea la tabla de nivel 2 si hace falta

    if (pte.valid) {
        policy.on_access(pte.frame);
    } else {
        result.page_fault = true;
        counters.page_faults++;
        handle_page_fault(result.vpn, pte, result);
    }
    pte.accessed = true;
    if (is_write) pte.dirty = true;

    result.frame = pte.frame;
    result.physical_address = layout.physical_address(pte.frame, layout.offset(address));
    return result;
}

// Consigue un marco (libre o expulsando a otra página) y carga la página en él.
void VirtualMemory::handle_page_fault(uint32_t vpn, PageTableEntry& pte, AccessResult& result) {
    uint32_t frame = memory.has_free_frame() ? memory.take_free_frame() : evict_page(result);

    auto saved = swap.find(vpn);
    if (saved != swap.end()) {
        memory.load_page(frame, saved->second);  // vuelve del disco con su contenido
        result.loaded_from_disk = true;
    } else {
        memory.zero_page(frame);  // página nueva: llega en ceros
    }

    pte.frame = frame;
    pte.valid = true;
    pte.accessed = false;
    pte.dirty = false;
    frame_owner[frame] = vpn;
    policy.on_load(frame);
}

// Aplica la política de reemplazo y desaloja la página elegida. Devuelve el marco que quedó libre.
uint32_t VirtualMemory::evict_page(AccessResult& result) {
    uint32_t frame = policy.choose_victim();
    uint32_t victim_vpn = frame_owner[frame];
    PageTableEntry& victim = table.entry(victim_vpn);

    if (victim.dirty) {  // solo las páginas modificadas se escriben a disco; las limpias se descartan
        swap[victim_vpn] = memory.copy_page(frame);
        counters.writebacks++;
        result.written_back = true;
    }
    victim.valid = false;

    counters.replacements++;
    result.replaced = true;
    result.evicted_vpn = victim_vpn;
    return frame;
}
