#include "domain/page_table.h"

PageTable::PageTable(const AddressLayout& l) : layout(l), directory(l.level1_entries()) {}

bool PageTable::has_level2(uint32_t vpn) const {
    return directory[layout.level1_index(vpn)] != nullptr;
}

PageTableEntry& PageTable::entry(uint32_t vpn) {
    std::unique_ptr<Level2Table>& table = directory[layout.level1_index(vpn)];
    if (table == nullptr) {
        table = std::make_unique<Level2Table>(layout.level2_entries());
        created++;
    }
    return (*table)[layout.level2_index(vpn)];
}

const PageTableEntry* PageTable::find(uint32_t vpn) const {
    const std::unique_ptr<Level2Table>& table = directory[layout.level1_index(vpn)];
    if (table == nullptr) return nullptr;
    return &(*table)[layout.level2_index(vpn)];
}
