#pragma once
#include <list>
#include <stdexcept>
#include "domain/replacement_policy.h"

// FIFO: se expulsa la página que lleva más tiempo en memoria.
// Los marcos entran a una cola cuando se cargan y salen por el frente al expulsarse.
class Fifo : public ReplacementPolicy {
public:
    std::string name() const override { return "FIFO"; }

    void on_load(uint32_t frame) override { queue.push_back(frame); }
    void on_access(uint32_t) override {}  // FIFO ignora los accesos: solo cuenta el orden de llegada
    void on_release(uint32_t frame) override { queue.remove(frame); }

    uint32_t choose_victim() override {
        if (queue.empty()) throw std::logic_error("FIFO: there is no page to evict");
        uint32_t frame = queue.front();
        queue.pop_front();
        return frame;
    }

private:
    std::list<uint32_t> queue;  // frente = la página más antigua
};
