#pragma once
#include <string>
#include <vector>
#include "domain/operation.h"

// Lee el programa de entrada. Una instrucción por línea:
//   alloc <bytes>
//   write <virtual_addr> <value>
//   read <virtual_addr>
//   free <virtual_addr>
// Los números pueden ir en decimal o en hexadecimal con prefijo 0x. Se ignoran las
// líneas vacías y los comentarios (#). Lanza std::runtime_error indicando la línea
// si el archivo está mal formado.
std::vector<Operation> read_program(const std::string& path);
