#pragma once
#include <cstdint>
#include <string>

// Interfaz de una política de reemplazo de páginas (patrón Strategy).
// La memoria virtual le avisa lo que pasa con los marcos y solo le pregunta una cosa:
// cuál marco expulsar cuando no queda ninguno libre. Así FIFO y LRU se intercambian
// sin tocar la traducción ni el manejo de fallos.
class ReplacementPolicy {
public:
    virtual ~ReplacementPolicy() {}

    virtual std::string name() const = 0;
    virtual void on_load(uint32_t frame) = 0;     // se cargó una página en este marco
    virtual void on_access(uint32_t frame) = 0;   // se accedió a la página de este marco (hit)
    virtual void on_release(uint32_t frame) = 0;  // el marco quedó libre (free): dejar de seguirlo
    virtual uint32_t choose_victim() = 0;         // elige un marco a expulsar y deja de seguirlo
};
