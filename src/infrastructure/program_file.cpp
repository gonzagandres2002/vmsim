#include "infrastructure/program_file.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

static std::runtime_error line_error(int line, const std::string& message) {
    return std::runtime_error("line " + std::to_string(line) + ": " + message);
}

// Convierte "4096" o "0x1000" a entero sin signo de 32 bits. Rechaza signos, texto sobrante
// y valores que no caben en 32 bits, para que un dato mal escrito nunca pase en silencio.
static uint32_t parse_number(const std::string& text, const std::string& what, int line) {
    bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    std::string digits = hex ? text.substr(2) : text;
    try {
        if (digits.empty() || digits[0] == '-' || digits[0] == '+') throw std::invalid_argument("sign");
        size_t used = 0;
        unsigned long long value = std::stoull(digits, &used, hex ? 16 : 10);
        if (used != digits.size()) throw std::invalid_argument("trailing characters");
        if (value > 0xFFFFFFFFull) throw std::out_of_range("32 bits");
        return (uint32_t)value;
    } catch (const std::exception&) {
        throw line_error(line, what + " must be a 32-bit unsigned integer (got '" + text + "')");
    }
}

static uint8_t parse_byte(const std::string& text, int line) {
    uint32_t value = parse_number(text, "value", line);
    if (value > 255) throw line_error(line, "value must be between 0 and 255: each address holds one byte");
    return (uint8_t)value;
}

static void expect_arguments(const std::vector<std::string>& tokens, size_t count, int line) {
    if (tokens.size() != count + 1)
        throw line_error(line, tokens[0] + " expects " + std::to_string(count) + " argument(s), found " +
                                   std::to_string(tokens.size() - 1));
}

static Operation parse_operation(const std::vector<std::string>& tokens, int line) {
    Operation op;
    op.line = line;
    const std::string& command = tokens[0];
    if (command == "alloc") {
        expect_arguments(tokens, 1, line);
        op.kind = OperationKind::Alloc;
        op.bytes = parse_number(tokens[1], "size", line);
        if (op.bytes == 0) throw line_error(line, "alloc size must be greater than 0");
    } else if (command == "write") {
        expect_arguments(tokens, 2, line);
        op.kind = OperationKind::Write;
        op.address = parse_number(tokens[1], "address", line);
        op.value = parse_byte(tokens[2], line);
    } else if (command == "read") {
        expect_arguments(tokens, 1, line);
        op.kind = OperationKind::Read;
        op.address = parse_number(tokens[1], "address", line);
    } else if (command == "free") {
        expect_arguments(tokens, 1, line);
        op.kind = OperationKind::Free;
        op.address = parse_number(tokens[1], "address", line);
    } else {
        throw line_error(line, "unknown command '" + command + "' (expected alloc, write, read or free)");
    }
    return op;
}

std::vector<Operation> read_program(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("could not open file " + path);

    std::vector<Operation> program;
    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        line_number++;
        if (!line.empty() && line.back() == '\r') line.pop_back();  // archivos de Windows
        size_t comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);  // comentarios: desde # hasta el final

        // Separar la línea por espacios en blanco.
        std::vector<std::string> tokens;
        std::istringstream words(line);
        std::string word;
        while (words >> word) tokens.push_back(word);
        if (tokens.empty()) continue;

        program.push_back(parse_operation(tokens, line_number));
    }

    if (program.empty()) throw std::runtime_error("the file contains no instructions");
    return program;
}
