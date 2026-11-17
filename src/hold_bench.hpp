// hold_bench.hpp
// ETAPA 4 - O harness do modelo HOLD (Jones, 1986), com reagendamentos.
//
// Modelo hold: mantém-se uma fila com n eventos e repete-se
//     pop_min  ->  push(mesmo visitante, tempo_atual + incremento aleatório)
// Cada repetição ("um hold") exercita a fila em regime estacionário, que é o
// que acontece num simulador: o tamanho fica ~n e o relógio avança.
// Extensão deste projeto: com probabilidade p, cada passo também REAGENDA um
// visitante aleatório (desistência da fila, quebra de brinquedo, buff...).
//
// Duas decisões de metodologia importantes:
//  1. A sequência de operações é PRÉ-GERADA (struct Workload) e reaproveitada
//     por todas as estruturas e repetições. Assim (a) o tempo do gerador
//     aleatório não entra na medição e (b) todas as estruturas processam
//     exatamente a mesma carga.
//  2. Cada execução devolve um CHECKSUM da sequência de ids extraídos. Se as
//     três estruturas estiverem corretas, os checksums são idênticos; se
//     diferirem, há bug (ou empate mal resolvido) e o tempo não vale nada.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "event.hpp"

namespace pq {

// Distribuição dos incrementos de tempo. Importa porque define ONDE o novo
// evento cai na fila (perto do próximo a sair ou longe), e isso muda muito o
// custo da inserção em lista/array ordenados. Todas têm média 1.
enum class Dist { Exp, Uniform, Bimodal };

inline const char* dist_name(Dist d) {
    switch (d) {
        case Dist::Exp:     return "exp";
        case Dist::Uniform: return "uniform";
        case Dist::Bimodal: return "bimodal";
    }
    return "?";
}

inline Dist parse_dist(const std::string& s) {
    if (s == "exp") return Dist::Exp;
    if (s == "uniform") return Dist::Uniform;
    if (s == "bimodal") return Dist::Bimodal;
    throw std::invalid_argument("distribuicao desconhecida: " + s);
}

struct Workload {
    std::vector<double> init;                 // tempo inicial do evento de cada visitante
    std::vector<double> inc;                  // incremento do hold no passo i
    std::vector<std::uint8_t> do_resched;     // no passo i, também reagenda alguém?
    std::vector<std::uint32_t> victim;        // quem é reagendado
    std::vector<double> inc2;                 // atraso do reagendamento
};

inline Workload make_workload(std::size_t n, std::size_t ops, Dist d, double resched_frac,
                              std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u01(0.0, 1.0);
    std::exponential_distribution<double> exp1(1.0);
    auto sample = [&]() -> double {
        switch (d) {
            case Dist::Exp:     return exp1(rng);                  // média 1
            case Dist::Uniform: return 2.0 * u01(rng);             // média 1
            case Dist::Bimodal:                                    // média 0.8*0.2 + 0.2*4.2 = 1
                // 80% eventos curtos ("andar"), 20% longos ("passeio")
                return (u01(rng) < 0.8) ? 0.2 * exp1(rng) : 4.2 * exp1(rng);
        }
        return 1.0;
    };

    Workload w;
    w.init.resize(n);
    for (auto& t : w.init) t = sample();
    w.inc.resize(ops);
    w.do_resched.resize(ops);
    w.victim.resize(ops);
    w.inc2.resize(ops);
    for (std::size_t i = 0; i < ops; ++i) {
        w.inc[i] = sample();
        w.do_resched[i] = (u01(rng) < resched_frac) ? 1 : 0;
        w.victim[i] = static_cast<std::uint32_t>(rng() % n);
        w.inc2[i] = sample();
    }
    return w;
}

struct Result {
    double ns_per_hold = 0;       // tempo médio de um passo (pop + push [+ reschedule])
    std::uint64_t checksum = 0;   // hash da sequência de ids extraídos
};

// Q é qualquer classe que siga o CONTRATO descrito em heap_queue.hpp.
// Template (e não classe base virtual) para que a chamada seja direta, sem
// custo de despacho virtual poluindo a medição.
template <class Q>
Result run_hold(const Workload& w, std::size_t n) {
    const std::size_t ops = w.inc.size();
    Q q(n);
    std::uint64_t seq = 0;

    // Fase de preenchimento (fora do cronômetro).
    for (std::size_t id = 0; id < n; ++id)
        q.push(Event{w.init[id], seq++, static_cast<std::uint32_t>(id), EventType::Arrive});

    std::uint64_t checksum = 1469598103934665603ull;  // FNV-1a
    Event e{};

    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < ops; ++i) {
        if (!q.pop_min(e)) throw std::runtime_error("fila vazia no hold");
        checksum = (checksum ^ e.id) * 1099511628211ull;
        q.push(Event{e.time + w.inc[i], seq++, e.id, e.type});
        if (w.do_resched[i]) q.reschedule(w.victim[i], e.time + w.inc2[i], seq++);
    }
    const auto t1 = std::chrono::steady_clock::now();

    Result r;
    r.ns_per_hold = std::chrono::duration<double, std::nano>(t1 - t0).count() / ops;
    r.checksum = checksum ^ q.size();
    return r;
}

// Mediana de 'reps' execuções (robusta contra um pico de ruído do sistema).
template <class Q>
Result measure(const Workload& w, std::size_t n, int reps) {
    std::vector<double> t;
    Result last;
    for (int r = 0; r < reps; ++r) {
        last = run_hold<Q>(w, n);
        t.push_back(last.ns_per_hold);
    }
    std::sort(t.begin(), t.end());
    last.ns_per_hold = t[t.size() / 2];
    return last;
}

}  // namespace pq
