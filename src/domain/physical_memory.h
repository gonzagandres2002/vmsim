#pragma once
#include <cstdint>
#include <deque>
#include <vector>

// La memoria física: un arreglo de bytes dividido en marcos, más la lista de marcos libres.
class PhysicalMemory {
public:
    PhysicalMemory(uint32_t frame_count, uint32_t page_size);

    uint32_t frame_count() const { return (uint32_t)(bytes.size() / page_size); }
    bool has_free_frame() const { return !free_frames.empty(); }
    uint32_t take_free_frame();
    void release_frame(uint32_t frame);

    uint8_t read(uint32_t physical_address) const;
    void write(uint32_t physical_address, uint8_t value);

    // Contenido completo de un marco: para llevarlo al disco o traerlo de vuelta.
    std::vector<uint8_t> copy_page(uint32_t frame) const;
    void load_page(uint32_t frame, const std::vector<uint8_t>& content);
    void zero_page(uint32_t frame);

private:
    uint32_t page_size;
    std::vector<uint8_t> bytes;
    std::deque<uint32_t> free_frames;  // se entregan en orden (0, 1, 2...) para que las trazas sean predecibles

    void check_frame(uint32_t frame) const;
};
