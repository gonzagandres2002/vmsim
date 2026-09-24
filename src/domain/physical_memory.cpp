#include "domain/physical_memory.h"
#include <algorithm>
#include <stdexcept>
#include <string>

PhysicalMemory::PhysicalMemory(uint32_t frame_count, uint32_t page_size_bytes)
    : page_size(page_size_bytes), bytes((size_t)frame_count * page_size_bytes) {
    if (frame_count == 0) throw std::invalid_argument("physical memory needs at least one frame");
    for (uint32_t frame = 0; frame < frame_count; frame++) free_frames.push_back(frame);
}

uint32_t PhysicalMemory::take_free_frame() {
    if (free_frames.empty()) throw std::logic_error("take_free_frame called with no free frames");
    uint32_t frame = free_frames.front();
    free_frames.pop_front();
    return frame;
}

void PhysicalMemory::release_frame(uint32_t frame) {
    check_frame(frame);
    if (std::find(free_frames.begin(), free_frames.end(), frame) != free_frames.end())
        throw std::logic_error("frame " + std::to_string(frame) + " released twice");
    free_frames.push_back(frame);
}

uint8_t PhysicalMemory::read(uint32_t physical_address) const {
    if (physical_address >= bytes.size()) throw std::logic_error("physical address out of range");
    return bytes[physical_address];
}

void PhysicalMemory::write(uint32_t physical_address, uint8_t value) {
    if (physical_address >= bytes.size()) throw std::logic_error("physical address out of range");
    bytes[physical_address] = value;
}

std::vector<uint8_t> PhysicalMemory::copy_page(uint32_t frame) const {
    check_frame(frame);
    size_t start = (size_t)frame * page_size;
    return std::vector<uint8_t>(bytes.begin() + start, bytes.begin() + start + page_size);
}

void PhysicalMemory::load_page(uint32_t frame, const std::vector<uint8_t>& content) {
    check_frame(frame);
    if (content.size() != page_size) throw std::logic_error("page content has the wrong size");
    std::copy(content.begin(), content.end(), bytes.begin() + (size_t)frame * page_size);
}

void PhysicalMemory::zero_page(uint32_t frame) {
    check_frame(frame);
    size_t start = (size_t)frame * page_size;
    std::fill(bytes.begin() + start, bytes.begin() + start + page_size, 0);
}

void PhysicalMemory::check_frame(uint32_t frame) const {
    if (frame >= frame_count()) throw std::logic_error("frame " + std::to_string(frame) + " does not exist");
}
