// cli.hpp - utilitários mínimos de linha de comando (usados por ./bench e ./park).
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>

namespace cli {

inline std::vector<std::string> split(const std::string& s, char sep = ',') {
    std::vector<std::string> out(1);
    for (char c : s) {
        if (c == sep) out.emplace_back();
        else out.back() += c;
    }
    return out;
}

// Converte texto no tipo de destino (real, inteiro com sinal ou sem sinal).
template <class T>
T to_num(const std::string& s) {
    if constexpr (std::is_floating_point_v<T>) return static_cast<T>(std::stod(s));
    else if constexpr (std::is_signed_v<T>) return static_cast<T>(std::stoll(s));
    else return static_cast<T>(std::stoull(s));
}

// Percorre argv chamando on(opcao, next); next() devolve o valor da opção.
// on devolve false para uma opção desconhecida (o programa termina com erro).
template <class F>
void parse(int argc, char** argv, F&& on) {
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "faltou valor para %s\n", flag.c_str()); std::exit(2); }
            return argv[++i];
        };
        if (!on(flag, next)) { std::fprintf(stderr, "opcao desconhecida: %s\n", flag.c_str()); std::exit(2); }
    }
}

}  // namespace cli
