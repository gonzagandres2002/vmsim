#pragma once
#include <list>
#include <stdexcept>
#include "domain/replacement_policy.h"

// LRU (Least Recently Used): se expulsa la página que lleva más tiempo sin usarse.
// Cada acceso mueve el marco al final de la lista, así que el frente siempre es
// el menos recientemente usado.
class Lru : public ReplacementPolicy {
public:
    std::string name() const override { return "LRU"; }

    void on_load(uint32_t frame) override { order.push_back(frame); }
    void on_access(uint32_t frame) override {
        order.remove(frame);
        order.push_back(frame);
    }
    void on_release(uint32_t frame) override { order.remove(frame); }

    uint32_t choose_victim() override {
        if (order.empty()) throw std::logic_error("LRU: there is no page to evict");
        uint32_t frame = order.front();
        order.pop_front();
        return frame;
    }

private:
    std::list<uint32_t> order;  // frente = menos recientemente usado, final = más reciente
};
