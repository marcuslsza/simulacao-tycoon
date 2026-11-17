// test_park.cpp
// ETAPA 6 (testes) - Correção do simulador de parque.
//
// O simulador tem muita lógica própria (filas com remoção preguiçosa, quebras,
// cancelamentos), então ele também precisa de testes. Três verificações:
//
//  1. MESMA HISTÓRIA: as três filas de eventos devem produzir exatamente a
//     mesma simulação (checksum e todos os contadores iguais).
//  2. INVARIANTES: ao final, (a) todo visitante está em exatamente um estado,
//     (b) a soma dos tamanhos de fila bate com os visitantes em fila, e
//     (c) existe exatamente um evento pendente por entidade
//     (visitantes + atrações + gerador de incidentes).
//  3. COBERTURA: as configurações forçam cancelamentos, reagendamentos e
//     desistências, senão os testes passariam sem exercitar o que importa.
#include <cstdio>
#include <cstdlib>

#include "../src/heap_queue.hpp"
#include "../src/park_sim.hpp"
#include "../src/sorted_array_queue.hpp"
#include "../src/sorted_list_queue.hpp"

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

template <class Q>
pq::ParkStats run_and_check(const pq::ParkConfig& cfg) {
    pq::ParkSim<Q> sim(cfg);
    const pq::ParkStats st = sim.run();

    const std::size_t V = cfg.visitors, R = cfg.rides;
    using S = pq::VisitorState;
    const std::size_t idle = sim.count_state(S::Idle), walking = sim.count_state(S::Walking),
                      queued = sim.count_state(S::Queued), riding = sim.count_state(S::Riding);
    CHECK(idle + walking + queued + riding == V);

    std::size_t lines = 0;
    for (std::size_t r = 0; r < R; ++r) lines += sim.line_length(r);
    CHECK(lines == queued);

    CHECK(sim.pending() == V + R + 1);          // um evento pendente por entidade
    CHECK(sim.queue().check_invariant());       // a estrutura continua íntegra
    CHECK(st.events > 0);
    return st;
}

static bool same(const pq::ParkStats& a, const pq::ParkStats& b) {
    return a.checksum == b.checksum && a.events == b.events && a.n_push == b.n_push &&
           a.n_cancel == b.n_cancel && a.n_resched == b.n_resched && a.boarded == b.boarded &&
           a.giveups == b.giveups && a.balks == b.balks && a.completed == b.completed &&
           a.incidents == b.incidents && a.mean_wait == b.mean_wait &&
           a.peak_pending == b.peak_pending && a.peak_line == b.peak_line;
}

static void test_config(const char* label, const pq::ParkConfig& cfg, bool must_exercise) {
    const pq::ParkStats h = run_and_check<pq::IndexedBinaryHeap>(cfg);
    const pq::ParkStats a = run_and_check<pq::SortedArrayQueue>(cfg);
    const pq::ParkStats l = run_and_check<pq::SortedLinkedListQueue>(cfg);
    CHECK(same(h, a));
    CHECK(same(h, l));
    if (must_exercise) {
        CHECK(h.n_cancel > 0);   // embarques cancelam o GiveUp
        CHECK(h.n_resched > 0);  // quebras reagendam
        CHECK(h.giveups > 0);
        CHECK(h.incidents > 0);
        CHECK(h.boarded > 0);
    }
    std::printf("OK  %-22s eventos=%llu cancel=%llu resched=%llu desist=%llu\n", label,
                static_cast<unsigned long long>(h.events), static_cast<unsigned long long>(h.n_cancel),
                static_cast<unsigned long long>(h.n_resched), static_cast<unsigned long long>(h.giveups));
}

int main() {
    pq::ParkConfig c;

    c = {};  c.visitors = 300;  c.rides = 6;  c.minutes = 180;  c.seed = 7;
    test_config("parque pequeno", c, true);

    c = {};  c.visitors = 1500;  c.rides = 10;  c.minutes = 120;  c.seed = 11;  c.incident_mean = 3;
    test_config("muitas quebras", c, true);

    c = {};  c.visitors = 40;  c.rides = 2;  c.minutes = 90;  c.seed = 3;
    test_config("minusculo", c, false);

    c = {};  c.visitors = 800;  c.rides = 26;  c.minutes = 100;  c.seed = 5;  c.capacity = 3;
    test_config("26 atracoes", c, true);

    std::puts("Todos os testes do simulador passaram.");
    return 0;
}
