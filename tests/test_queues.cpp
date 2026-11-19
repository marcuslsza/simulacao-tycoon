// test_queues.cpp
// Testes de correção (rodar ANTES de qualquer benchmark).
//
// Um benchmark de estrutura com bug mede lixo. Aqui cada estrutura é submetida
// a milhares de operações aleatórias (push, pop_min, cancel, reschedule) e,
// a cada passo, comparada com um "oráculo": std::set, da biblioteca padrão,
// que mantém os eventos ordenados de forma comprovadamente correta.
//
// Os tempos são inteiros pequenos de propósito: geram MUITOS empates de 'time',
// o que força o desempate por 'seq' (a causa mais comum de bugs sutis).
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <random>
#include <set>
#include <vector>

#include "check.hpp"
#include "../src/event.hpp"
#include "../src/heap_queue.hpp"
#include "../src/sorted_array_queue.hpp"
#include "../src/sorted_list_queue.hpp"

struct Cmp {
    bool operator()(const pq::Event& a, const pq::Event& b) const { return pq::before(a, b); }
};

template <class Q>
void test_queue(std::size_t n_ids, std::size_t steps, std::uint64_t seed) {
    Q q(n_ids);
    std::set<pq::Event, Cmp> ref;                      // oráculo
    std::vector<std::optional<pq::Event>> cur(n_ids);  // evento pendente de cada id, no oráculo
    std::mt19937_64 rng(seed);
    std::uint64_t seq = 0;

    for (std::size_t s = 0; s < steps; ++s) {
        const int op = static_cast<int>(rng() % 4);
        const auto id = static_cast<std::uint32_t>(rng() % n_ids);
        const double time = static_cast<double>(rng() % 50);

        if (op == 0) {  // push (só se o visitante não tem evento pendente)
            if (!cur[id]) {
                pq::Event e{time, seq++, id, pq::EventType::Arrive};
                q.push(e);
                ref.insert(e);
                cur[id] = e;
            }
        } else if (op == 1) {  // pop_min
            pq::Event got{};
            const bool ok = q.pop_min(got);
            CHECK(ok == !ref.empty());
            if (ok) {
                const pq::Event want = *ref.begin();
                CHECK(got.id == want.id && got.seq == want.seq && got.time == want.time);
                ref.erase(ref.begin());
                cur[want.id].reset();
            }
        } else if (op == 2) {  // cancel
            const bool ok = q.cancel(id);
            CHECK(ok == cur[id].has_value());
            if (ok) {
                ref.erase(*cur[id]);
                cur[id].reset();
            }
        } else {  // reschedule (a chave pode subir ou descer)
            const std::uint64_t s2 = seq++;
            const bool ok = q.reschedule(id, time, s2);
            CHECK(ok == cur[id].has_value());
            if (ok) {
                ref.erase(*cur[id]);
                pq::Event e = *cur[id];
                e.time = time;
                e.seq = s2;
                ref.insert(e);
                cur[id] = e;
            }
        }
        CHECK(q.size() == ref.size());
        if (s % 17 == 0) CHECK(q.check_invariant());
    }

    // Esvazia e confere a ordem completa.
    pq::Event got{};
    while (!ref.empty()) {
        CHECK(q.pop_min(got));
        const pq::Event want = *ref.begin();
        CHECK(got.id == want.id && got.seq == want.seq && got.time == want.time);
        ref.erase(ref.begin());
    }
    CHECK(!q.pop_min(got));
    CHECK(q.size() == 0 && q.check_invariant());
}

template <class Q>
void run_all() {
    test_queue<Q>(1, 20000, 1);      // caso extremo: um único visitante
    test_queue<Q>(8, 200000, 2);     // fila pequena: esvazia e enche toda hora
    test_queue<Q>(500, 200000, 3);   // fila média
    test_queue<Q>(5000, 200000, 4);  // fila maior
    std::printf("OK  %s\n", Q::name());
}

int main() {
    run_all<pq::IndexedBinaryHeap>();
    run_all<pq::SortedArrayQueue>();
    run_all<pq::SortedLinkedListQueue>();
    std::puts("Todos os testes passaram.");
    return 0;
}
