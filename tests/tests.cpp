// Pruebas mínimas con assert: si una falla, el programa aborta indicando el archivo y la línea.
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "application/simulator.h"
#include "domain/fifo.h"
#include "domain/lru.h"
#include "domain/virtual_memory.h"
#include "infrastructure/program_file.h"

// true si la acción lanza una excepción. Se usa como: assert(fails([&] { vm.read(0); }));
template <typename Action>
static bool fails(Action action) {
    try {
        action();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

static void test_address_layout() {
    AddressLayout layout(4096);
    assert(layout.offset_bits == 12 && layout.level2_bits == 10 && layout.level1_bits == 10);
    assert(layout.level1_entries() == 1024 && layout.level2_entries() == 1024);

    uint32_t va = 0x00403004;  // PT1 = 1, PT2 = 3, offset = 4
    assert(layout.offset(va) == 4);
    assert(layout.vpn(va) == 0x403);
    assert(layout.level1_index(layout.vpn(va)) == 1 && layout.level2_index(layout.vpn(va)) == 3);
    assert(layout.physical_address(5, 4) == 0x5004);
    assert(hex_address(4096) == "0x00001000");

    AddressLayout big(8192);  // 13 bits de offset, 19 de VPN: 9 para PT2 y 10 para PT1
    assert(big.offset_bits == 13 && big.level2_bits == 9 && big.level1_bits == 10);
    assert(fails([] { AddressLayout bad(3000); }));  // no es potencia de 2
}

static void test_page_table_creates_level2_on_demand() {
    AddressLayout layout(4096);
    PageTable table(layout);
    assert(!table.has_level2(0) && table.find(0) == nullptr && table.level2_count() == 0);

    PageTableEntry& pte = table.entry(0);  // crea la tabla de nivel 2 del directorio[0]
    assert(!pte.valid && table.has_level2(0) && table.level2_count() == 1);
    pte.valid = true;
    pte.frame = 7;
    assert(table.find(0)->frame == 7);

    table.entry(1023);  // misma tabla de nivel 2 (PT1 = 0)
    assert(table.level2_count() == 1);
    table.entry(1024);  // PT1 = 1: otra tabla
    assert(table.level2_count() == 2 && table.has_level2(1024));
    assert(table.find(2048) == nullptr);
}

static void test_physical_memory() {
    PhysicalMemory memory(3, 4096);
    assert(memory.frame_count() == 3 && memory.has_free_frame());
    assert(memory.take_free_frame() == 0 && memory.take_free_frame() == 1 && memory.take_free_frame() == 2);
    assert(!memory.has_free_frame());
    assert(fails([&] { memory.take_free_frame(); }));
    memory.release_frame(1);
    assert(fails([&] { memory.release_frame(1); }));  // doble liberación
    assert(memory.take_free_frame() == 1);

    memory.write(0x1004, 42);
    assert(memory.read(0x1004) == 42);
    std::vector<uint8_t> page = memory.copy_page(1);
    assert(page.size() == 4096 && page[4] == 42);
    memory.zero_page(1);
    assert(memory.read(0x1004) == 0);
    memory.load_page(1, page);
    assert(memory.read(0x1004) == 42);
    assert(fails([&] { memory.read(3 * 4096); }));  // fuera de la memoria
}

static void test_fifo() {
    Fifo fifo;
    assert(fifo.name() == "FIFO");
    fifo.on_load(0);
    fifo.on_load(1);
    fifo.on_load(2);
    fifo.on_access(0);  // FIFO no cambia con los accesos
    assert(fifo.choose_victim() == 0);
    fifo.on_load(0);     // [1, 2, 0]
    fifo.on_release(2);  // [1, 0]
    assert(fifo.choose_victim() == 1);
    assert(fifo.choose_victim() == 0);
    assert(fails([&] { fifo.choose_victim(); }));
}

static void test_lru() {
    Lru lru;
    assert(lru.name() == "LRU");
    lru.on_load(0);
    lru.on_load(1);
    lru.on_load(2);     // [0, 1, 2]
    lru.on_access(0);   // [1, 2, 0]: el 0 pasa a ser el más reciente
    assert(lru.choose_victim() == 1);
    lru.on_load(1);     // [2, 0, 1]
    lru.on_release(0);  // [2, 1]
    assert(lru.choose_victim() == 2);
    assert(lru.choose_victim() == 1);
    assert(fails([&] { lru.choose_victim(); }));
}

static void test_statement_example() {
    Fifo policy;
    VirtualMemory vm(AddressLayout(4096), 64, policy);

    AllocResult region = vm.alloc(8192);
    assert(region.start == 0 && region.pages == 2);

    AccessResult w1 = vm.write(0, 42);
    assert(w1.page_fault && w1.level2_created && !w1.replaced && !w1.loaded_from_disk);
    assert(w1.vpn == 0 && w1.frame == 0 && w1.physical_address == 0);
    AccessResult w2 = vm.write(4096, 99);
    assert(w2.page_fault && !w2.level2_created && w2.frame == 1 && w2.physical_address == 0x1000);
    AccessResult r1 = vm.read(0);
    assert(!r1.page_fault && r1.value == 42 && r1.physical_address == 0);
    AccessResult r2 = vm.read(4096);
    assert(!r2.page_fault && r2.value == 99 && r2.frame == 1);

    const Stats& s = vm.stats();
    assert(s.accesses == 4 && s.page_faults == 2 && s.hits() == 2 && s.replacements == 0 && s.writebacks == 0);
    assert(s.hit_rate() == 50.0);
    assert(vm.page_table().level2_count() == 1);
    const PageTableEntry* pte = vm.page_table().find(0);
    assert(pte->valid && pte->accessed && pte->dirty && pte->frame == 0);

    // El offset se conserva y una página solo leída no queda sucia.
    AccessResult w3 = vm.write(4100, 5);
    assert(!w3.page_fault && w3.physical_address == 0x1004 && vm.read(4100).value == 5);
    AllocResult second = vm.alloc(8193);  // 3 páginas, justo después de la primera región
    assert(second.start == 8192 && second.pages == 3);
    AccessResult r3 = vm.read(8192);
    assert(r3.page_fault && r3.frame == 2 && r3.value == 0);
    const PageTableEntry* clean = vm.page_table().find(2);
    assert(clean->valid && clean->accessed && !clean->dirty);
}

static void test_segmentation_fault() {
    Fifo policy;
    VirtualMemory vm(AddressLayout(4096), 4, policy);
    assert(fails([&] { vm.read(0); }));  // nada reservado todavía
    vm.alloc(4096);
    vm.read(4095);  // última dirección válida de la región
    assert(fails([&] { vm.read(4096); }));
    assert(fails([&] { vm.write(4096, 1); }));
    assert(vm.stats().accesses == 1);  // los accesos inválidos no se cuentan
    assert(fails([&] { vm.alloc(0); }));
}

// Cuántos fallos produce una secuencia de páginas con cierta política y cierto número de marcos.
static uint64_t faults_for(ReplacementPolicy& policy, uint32_t frames, const std::vector<uint32_t>& pages) {
    VirtualMemory vm(AddressLayout(4096), frames, policy);
    uint32_t last_page = *std::max_element(pages.begin(), pages.end());
    vm.alloc((last_page + 1) * 4096);
    for (uint32_t page : pages) vm.read(page * 4096);
    return vm.stats().page_faults;
}

static void test_reference_strings() {
    // OSTEP cap. 22 (y diapositivas de clase), 3 marcos: FIFO 7 fallos, LRU 5.
    std::vector<uint32_t> ostep = {0, 1, 2, 0, 1, 3, 0, 3, 1, 2, 1};
    Fifo fifo3;
    assert(faults_for(fifo3, 3, ostep) == 7);
    Lru lru3;
    assert(faults_for(lru3, 3, ostep) == 5);

    // Anomalía de Belady: FIFO empeora al pasar de 3 a 4 marcos; LRU no.
    std::vector<uint32_t> belady = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    Fifo fifo_a, fifo_b;
    assert(faults_for(fifo_a, 3, belady) == 9);
    assert(faults_for(fifo_b, 4, belady) == 10);
    Lru lru_a, lru_b;
    assert(faults_for(lru_a, 3, belady) == 10);
    assert(faults_for(lru_b, 4, belady) == 8);
}

static void test_dirty_pages_are_written_back_and_restored() {
    Fifo policy;
    VirtualMemory vm(AddressLayout(4096), 2, policy);
    vm.alloc(3 * 4096);
    vm.write(0, 7);     // VPN 0 -> marco 0, sucia
    vm.write(4096, 8);  // VPN 1 -> marco 1, sucia; la memoria está llena

    AccessResult a = vm.read(8192);  // expulsa VPN 0 (sucia: va al disco)
    assert(a.page_fault && a.replaced && a.evicted_vpn == 0 && a.written_back && !a.loaded_from_disk);
    assert(a.frame == 0 && a.value == 0);
    assert(vm.stats().replacements == 1 && vm.stats().writebacks == 1);
    assert(!vm.page_table().find(0)->valid);

    AccessResult b = vm.read(0);  // expulsa VPN 1 (sucia); VPN 0 vuelve del disco con su 7
    assert(b.replaced && b.evicted_vpn == 1 && b.written_back && b.loaded_from_disk && b.value == 7 && b.frame == 1);
    assert(vm.stats().writebacks == 2);

    AccessResult c = vm.read(4096);  // expulsa VPN 2 (limpia: se descarta); VPN 1 vuelve con su 8
    assert(c.replaced && c.evicted_vpn == 2 && !c.written_back && c.loaded_from_disk && c.value == 8);
    assert(vm.stats().writebacks == 2);

    AccessResult d = vm.read(0);
    assert(!d.page_fault && d.value == 7);

    AccessResult e = vm.read(8192);  // expulsa VPN 0: volvió del disco y no se escribió -> limpia
    assert(e.replaced && e.evicted_vpn == 0 && !e.written_back && !e.loaded_from_disk && e.value == 0);
    assert(vm.stats().accesses == 7 && vm.stats().page_faults == 6 && vm.stats().replacements == 4);
    assert(vm.stats().writebacks == 2);
}

static void test_free_releases_frames() {
    Fifo policy;
    VirtualMemory vm(AddressLayout(4096), 2, policy);
    AllocResult a = vm.alloc(4096);  // VPN 0
    AllocResult b = vm.alloc(4096);  // VPN 1
    AllocResult c = vm.alloc(4096);  // VPN 2
    assert(a.start == 0 && b.start == 4096 && c.start == 8192);
    vm.write(a.start, 1);
    vm.write(b.start, 2);  // marcos 0 y 1: memoria llena

    FreeResult freed = vm.free(a.start);
    assert(freed.pages == 1 && freed.frames_released == 1);
    assert(!vm.page_table().find(0)->valid);
    AccessResult r = vm.read(c.start);  // hay un marco libre: no hace falta reemplazar
    assert(r.page_fault && !r.replaced && r.frame == 0);
    assert(vm.stats().replacements == 0);

    assert(fails([&] { vm.read(a.start); }));       // la región ya no existe
    assert(fails([&] { vm.free(a.start); }));       // doble free
    assert(fails([&] { vm.free(b.start + 100); })); // no es el inicio de una región

    vm.write(b.start, 5);
    AllocResult d = vm.alloc(4096);
    AccessResult r2 = vm.read(d.start);  // expulsa VPN 1 (marco 1, sucia) al disco
    assert(r2.replaced && r2.evicted_vpn == 1 && r2.written_back);
    FreeResult freed_b = vm.free(b.start);
    assert(freed_b.pages == 1 && freed_b.frames_released == 0);  // su página estaba en disco, no en memoria
}

static void test_page_size_is_configurable() {
    Fifo policy;
    VirtualMemory vm(AddressLayout(8192), 2, policy);
    AllocResult a = vm.alloc(8192);
    assert(a.pages == 1);
    AccessResult w = vm.write(8191, 3);
    assert(w.vpn == 0 && w.physical_address == 8191);
    AllocResult b = vm.alloc(1);
    assert(b.start == 8192 && b.pages == 1);
    AccessResult r = vm.read(8192);
    assert(r.vpn == 1 && r.frame == 1 && r.physical_address == 8192 && r.value == 0);
}

static void test_stats() {
    Stats s;
    s.accesses = 4;
    s.page_faults = 2;
    assert(s.hits() == 2 && s.hit_rate() == 50.0);
    assert(s.estimated_time_ns() == 4 * 100 + 2 * 10000000ull);
    s.writebacks = 1;
    assert(s.estimated_time_ns() == 4 * 100 + 3 * 10000000ull);
    Stats empty;
    assert(empty.hit_rate() == 0.0 && empty.estimated_time_ns() == 0);
}

static std::string read_program_error(const std::string& content) {
    std::ofstream file("/tmp/vmsim_test_program.txt");
    file << content;
    file.close();
    try {
        read_program("/tmp/vmsim_test_program.txt");
        return "";
    } catch (const std::runtime_error& e) {
        return e.what();
    }
}

static void test_read_program() {
    std::vector<Operation> program = read_program("data/example.txt");
    assert(program.size() == 5);
    assert(program[0].kind == OperationKind::Alloc && program[0].bytes == 8192 && program[0].line == 2);
    assert(program[1].kind == OperationKind::Write && program[1].address == 0 && program[1].value == 42);
    assert(program[4].kind == OperationKind::Read && program[4].address == 4096 && program[4].line == 6);

    std::ofstream file("/tmp/vmsim_test_program.txt");
    file << "alloc 0x2000\r\nread 0x1000  # comentario al final\r\n\nfree 0\n";  // con finales de línea de Windows
    file.close();
    std::vector<Operation> hex = read_program("/tmp/vmsim_test_program.txt");
    assert(hex.size() == 3 && hex[0].bytes == 8192 && hex[1].address == 4096 && hex[2].kind == OperationKind::Free);

    assert(read_program_error("alloc 4096\nwrite 0 300\n").find("line 2") != std::string::npos);
    assert(read_program_error("read abc\n").find("line 1") != std::string::npos);
    assert(read_program_error("read -5\n").find("line 1") != std::string::npos);
    assert(read_program_error("read 0x100000000\n").find("32-bit") != std::string::npos);
    assert(read_program_error("jump 5\n").find("unknown command") != std::string::npos);
    assert(read_program_error("read\n").find("expects 1 argument") != std::string::npos);
    assert(read_program_error("alloc 0\n").find("greater than 0") != std::string::npos);
    assert(read_program_error("# solo comentarios\n").find("no instructions") != std::string::npos);
    assert(fails([] { read_program("data/does_not_exist.txt"); }));
}

static void test_run_program() {
    std::vector<Operation> program = read_program("data/example.txt");
    Lru policy;
    VirtualMemory vm(AddressLayout(4096), 64, policy);
    SimulationResult result = run_program(program, vm);
    assert(result.trace.size() == 5 && result.stats.accesses == 4 && result.stats.page_faults == 2);
    assert(result.trace[0].allocated.pages == 2 && result.trace[3].access.value == 42);

    Operation bad;  // leer sin haber reservado: el error debe indicar la línea
    bad.kind = OperationKind::Read;
    bad.address = 0;
    bad.line = 7;
    Fifo other;
    VirtualMemory vm2(AddressLayout(4096), 1, other);
    std::string message;
    try {
        run_program({bad}, vm2);
    } catch (const std::runtime_error& e) {
        message = e.what();
    }
    assert(message.find("line 7") != std::string::npos && message.find("segmentation fault") != std::string::npos);
}

int main() {
    test_address_layout();
    test_page_table_creates_level2_on_demand();
    test_physical_memory();
    test_fifo();
    test_lru();
    test_statement_example();
    test_segmentation_fault();
    test_reference_strings();
    test_dirty_pages_are_written_back_and_restored();
    test_free_releases_frames();
    test_page_size_is_configurable();
    test_stats();
    test_read_program();
    test_run_program();
    std::cout << "All tests passed (14)\n";
    return 0;
}
