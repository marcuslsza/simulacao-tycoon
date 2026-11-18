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
//  3. COBERTURA: as configurações forçam cancelamentos, reagendamentos,
//     desistências e recusas de fila, senão os testes passariam sem exercitar
//     o que importa.
//  4. PARÂMETROS DAS ATRAÇÕES: duração e capacidade ficam dentro das faixas
//     pedidas, variam entre atrações e nunca são excedidas.
//  5. MODELO DE DECISÃO: a utilidade segue a fórmula (atratividade fixa, espera
//     estimada, distância, cansaço) e o cansaço muda o comportamento.
//  6. VARIEDADE: com a penalidade infinita (padrão) ninguém anda duas vezes
//     seguidas na mesma atração; com 0 isso volta a acontecer; recusar a fila
//     não conta como passeio; com 1 única atração a regra não se aplica.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

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

    for (std::size_t r = 0; r < R; ++r) CHECK(sim.riding(r) <= sim.capacity(r));  // capacidade respeitada
    CHECK(sim.pending() == V + R + 1);          // um evento pendente por entidade
    CHECK(sim.queue().check_invariant());       // a estrutura continua íntegra
    CHECK(st.events > 0);
    return st;
}

static bool same(const pq::ParkStats& a, const pq::ParkStats& b) {
    return a.checksum == b.checksum && a.events == b.events && a.n_push == b.n_push &&
           a.n_cancel == b.n_cancel && a.n_resched == b.n_resched && a.boarded == b.boarded &&
           a.giveups == b.giveups && a.balks == b.balks && a.completed == b.completed &&
           a.incidents == b.incidents && a.mean_wait == b.mean_wait && a.rests == b.rests &&
           a.repeats == b.repeats && a.mean_fatigue == b.mean_fatigue &&
           a.peak_pending == b.peak_pending && a.peak_line == b.peak_line;
}

static void test_ride_parameters() {
    // Faixa explícita: cada atração fica dentro dela e há variação entre atrações.
    pq::ParkConfig c;
    c.visitors = 200; c.rides = 20; c.minutes = 30; c.seed = 9;
    c.capacity_min = 10; c.capacity_max = 40; c.ride_min = 2.0; c.ride_max = 10.0;
    pq::ParkSim<pq::IndexedBinaryHeap> sim(c);
    bool varies_cap = false, varies_dur = false;
    for (std::size_t r = 0; r < c.rides; ++r) {
        CHECK(sim.capacity(r) >= 10 && sim.capacity(r) <= 40);
        CHECK(sim.ride_duration(r) >= 2.0 && sim.ride_duration(r) <= 10.0);
        varies_cap = varies_cap || sim.capacity(r) != sim.capacity(0);
        varies_dur = varies_dur || sim.ride_duration(r) != sim.ride_duration(0);
    }
    CHECK(varies_cap && varies_dur);

    // min == max: todas iguais (modo "capacidade fixa para todas").
    c.capacity_min = c.capacity_max = 12;
    pq::ParkSim<pq::SortedArrayQueue> fixed(c);
    for (std::size_t r = 0; r < c.rides; ++r) CHECK(fixed.capacity(r) == 12);

    // Os parâmetros dependem só da semente, não da estrutura de fila.
    pq::ParkSim<pq::SortedLinkedListQueue> other(c);
    for (std::size_t r = 0; r < c.rides; ++r)
        CHECK(other.ride_duration(r) == fixed.ride_duration(r));
    std::printf("OK  %-22s capacidade e duracao dentro das faixas, com variacao\n", "parametros das atracoes");
}

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

static void test_decision_model() {
    using Sim = pq::ParkSim<pq::IndexedBinaryHeap>;
    pq::ParkConfig c;
    c.visitors = 50; c.rides = 8; c.minutes = 60; c.seed = 21;
    c.attr_min = 10; c.attr_max = 40; c.w_wait = 1.0; c.w_dist = 3.0;

    // (1) Atratividade fixa dentro da faixa, variando entre atrações; e a fórmula
    //     da utilidade num parque recém-criado (filas vazias, ninguém cansado):
    //     U = atratividade - w_dist * (0.2 + 0.04 * distancia_manhattan_da_entrada).
    {
        Sim fresh(c);
        bool varies = false;
        for (std::size_t r = 0; r < c.rides; ++r) {
            CHECK(fresh.attractiveness(r) >= 10 && fresh.attractiveness(r) <= 40);
            varies = varies || fresh.attractiveness(r) != fresh.attractiveness(0);
            const int d = std::abs(fresh.ride_x(r) - fresh.ride_x(c.rides)) +
                          std::abs(fresh.ride_y(r) - fresh.entrance_y());
            CHECK(near(fresh.utility(0, r), fresh.attractiveness(r) - 3.0 * (0.2 + 0.04 * d)));
        }
        CHECK(varies);
    }

    // (2) Distância: com atratividades iguais e sem espera, quem está mais perto
    //     tem utilidade maior.
    {
        pq::ParkConfig e = c;
        e.attr_min = e.attr_max = 20;
        Sim fresh(e);
        for (std::size_t i = 0; i < e.rides; ++i)
            for (std::size_t j = 0; j < e.rides; ++j) {
                const int di = std::abs(fresh.ride_x(i) - fresh.ride_x(e.rides)) +
                               std::abs(fresh.ride_y(i) - fresh.entrance_y());
                const int dj = std::abs(fresh.ride_x(j) - fresh.ride_x(e.rides)) +
                               std::abs(fresh.ride_y(j) - fresh.entrance_y());
                if (di < dj) CHECK(fresh.utility(0, i) > fresh.utility(0, j));
            }
        e.w_dist = 0;  // sem peso de distância (e sem espera): só a atratividade conta
        Sim flat(e);
        for (std::size_t r = 0; r < e.rides; ++r) CHECK(near(flat.utility(0, r), 20.0));
    }

    // (3) Espera: depois de simular, U = atratividade - w_wait * fila/capacidade * intervalo
    //     (com w_dist = 0 e sem efeito do cansaço, m = 1).
    {
        pq::ParkConfig e = c;
        e.visitors = 600; e.minutes = 90; e.w_dist = 0; e.fatigue_sens = 0;
        e.repeat_penalty = 0;  // isola a espera: sem o termo de variedade
        Sim sim(e);
        sim.run();
        for (std::uint32_t v = 0; v < 5; ++v)
            for (std::size_t r = 0; r < e.rides; ++r) {
                const double wait = static_cast<double>(sim.line_length(r)) /
                                    static_cast<double>(sim.capacity(r)) *
                                    (sim.ride_duration(r) + e.load_time);
                CHECK(near(sim.utility(v, r), sim.attractiveness(r) - e.w_wait * wait));
            }
    }

    // (4) Cansaço: desligado => ninguém descansa; ligado => descansam, o cansaço
    //     médio é positivo e há MENOS embarques (visitantes cansados descansam).
    {
        pq::ParkConfig off = c;
        off.visitors = 800; off.rides = 8; off.minutes = 240;
        off.fatigue_per_ride = 0.0; off.rest_base = -1000.0;
        pq::ParkConfig on = off;
        on.fatigue_per_ride = 0.25; on.rest_base = 0.0;
        const pq::ParkStats s_off = run_and_check<pq::IndexedBinaryHeap>(off);
        const pq::ParkStats s_on = run_and_check<pq::IndexedBinaryHeap>(on);
        CHECK(s_off.rests == 0 && s_off.mean_fatigue == 0.0);
        CHECK(s_on.rests > 0 && s_on.mean_fatigue > 0.0);
        CHECK(s_on.boarded < s_off.boarded);

        Sim sim(on);
        sim.run();
        for (std::uint32_t v = 0; v < on.visitors; ++v) CHECK(sim.fatigue(v) >= 0.0);
    }
    std::printf("OK  %-22s utilidade (atratividade, distancia, espera) e cansaco\n", "modelo de decisao");
}

static void test_variety() {
    using Sim = pq::ParkSim<pq::IndexedBinaryHeap>;
    const double inf = std::numeric_limits<double>::infinity();

    // (1) Padrão (penalidade infinita): nenhuma repetição em sequência, nas três filas
    //     e em configurações bem diferentes. Inclui temperatura altíssima (escolha quase
    //     aleatória), que só passa se a proibição for exata e não "apenas improvável".
    {
        pq::ParkConfig base;
        base.visitors = 800; base.rides = 8; base.minutes = 240; base.seed = 31;
        CHECK(std::isinf(base.repeat_penalty));  // o padrão é proibir

        pq::ParkConfig two = base;   two.visitors = 300; two.rides = 2;
        pq::ParkConfig all = base;   all.consider = 0; all.temperature = 2.0; all.fatigue_per_ride = 0.5;
        pq::ParkConfig hot = base;   hot.temperature = 200.0;
        pq::ParkConfig norest = base; norest.rest_base = -1000.0;  // sem descanso: só passeia
        const pq::ParkConfig cfgs[] = {base, two, all, hot, norest};
        for (const pq::ParkConfig& c : cfgs) {
            const pq::ParkStats h = run_and_check<pq::IndexedBinaryHeap>(c);
            const pq::ParkStats a = run_and_check<pq::SortedArrayQueue>(c);
            const pq::ParkStats l = run_and_check<pq::SortedLinkedListQueue>(c);
            CHECK(same(h, a) && same(h, l));
            CHECK(h.completed > c.visitors);   // houve passeios de sobra (não é vacuamente verdade)
            CHECK(h.repeats == 0);
        }
    }

    // (2) A regra tem efeito: sem ela (penalidade 0) as repetições aparecem; com penalidade
    //     finita elas ficam raras, mas ainda possíveis (preferência, não proibição).
    {
        pq::ParkConfig c;
        c.visitors = 1500; c.rides = 8; c.minutes = 240; c.seed = 31;
        c.repeat_penalty = 0;
        const std::uint64_t free_rep = run_and_check<pq::IndexedBinaryHeap>(c).repeats;
        c.repeat_penalty = 15;
        const std::uint64_t soft_rep = run_and_check<pq::IndexedBinaryHeap>(c).repeats;
        c.repeat_penalty = inf;
        const std::uint64_t hard_rep = run_and_check<pq::IndexedBinaryHeap>(c).repeats;
        CHECK(free_rep > 0);
        CHECK(soft_rep < free_rep);
        CHECK(hard_rep == 0);
    }

    // (3) Fórmula: só a ÚLTIMA atração em que o visitante andou é penalizada (isolando a
    //     espera: w_dist = 0, sem efeito do cansaço). Finito => U = base - penalidade;
    //     infinito => -inf.
    for (const double pen : {15.0, inf}) {
        pq::ParkConfig e;
        e.visitors = 600; e.rides = 8; e.minutes = 90; e.seed = 21;
        e.w_dist = 0; e.fatigue_sens = 0; e.repeat_penalty = pen;
        Sim sim(e);
        sim.run();
        std::size_t with_last = 0;
        for (std::uint32_t v = 0; v < e.visitors; ++v) {
            const std::size_t last = sim.last_ride(v);
            CHECK(last <= e.rides);                       // índice válido, ou R = nenhuma
            with_last += last < e.rides;
            for (std::size_t r = 0; r < e.rides; ++r) {
                const double wait = static_cast<double>(sim.line_length(r)) /
                                    static_cast<double>(sim.capacity(r)) *
                                    (sim.ride_duration(r) + e.load_time);
                const double base = sim.attractiveness(r) - e.w_wait * wait;
                if (r == last) {
                    if (std::isinf(pen)) CHECK(std::isinf(sim.utility(v, r)) && sim.utility(v, r) < 0);
                    else CHECK(near(sim.utility(v, r), base - pen));
                } else {
                    CHECK(near(sim.utility(v, r), base));
                }
            }
        }
        CHECK(with_last > 0);
    }

    // (4) Recusar a fila (ou desistir) não é passear: se ninguém consegue entrar em fila
    //     nenhuma, ninguém "tem última atração" e nenhuma atração é penalizada.
    {
        pq::ParkConfig e;
        e.visitors = 200; e.rides = 5; e.minutes = 60; e.seed = 4;
        e.balk_wait = -1.0;  // toda espera estimada (>= 0) é longa demais: todos recusam
        Sim sim(e);
        const pq::ParkStats st = sim.run();
        CHECK(st.balks > 0 && st.completed == 0 && st.repeats == 0);
        for (std::uint32_t v = 0; v < e.visitors; ++v) {
            CHECK(sim.last_ride(v) == e.rides);
            for (std::size_t r = 0; r < e.rides; ++r) CHECK(std::isfinite(sim.utility(v, r)));
        }
    }

    // (5) Com UMA atração a regra não se aplica (senão cada visitante andaria uma vez e
    //     nunca mais): eles repetem, e o parque continua consistente.
    {
        pq::ParkConfig e;
        e.visitors = 60; e.rides = 1; e.minutes = 240; e.seed = 8;
        const pq::ParkStats h = run_and_check<pq::IndexedBinaryHeap>(e);
        CHECK(h.repeats > 0);
        CHECK(h.completed > e.visitors);
    }

    // (6) Valor inválido é rejeitado (negativo ou NaN); infinito e zero são aceitos.
    {
        pq::ParkConfig e;
        e.visitors = 10; e.rides = 3; e.minutes = 10;
        bool threw = false;
        e.repeat_penalty = -1.0;
        try { Sim s(e); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
        threw = false;
        e.repeat_penalty = std::numeric_limits<double>::quiet_NaN();
        try { Sim s(e); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
        e.repeat_penalty = 0.0;  Sim a(e);
        e.repeat_penalty = inf;  Sim b(e);
    }
    std::printf("OK  %-22s sem repeticao seguida por padrao; 0 liga de volta; recusa nao conta\n", "variedade");
}

static void test_config(const char* label, const pq::ParkConfig& cfg, bool must_exercise,
                        bool must_balk = false) {
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
    if (must_balk) CHECK(h.balks > 0);
    std::printf("OK  %-22s eventos=%llu cancel=%llu resched=%llu desist=%llu recusas=%llu\n", label,
                static_cast<unsigned long long>(h.events), static_cast<unsigned long long>(h.n_cancel),
                static_cast<unsigned long long>(h.n_resched), static_cast<unsigned long long>(h.giveups),
                static_cast<unsigned long long>(h.balks));
}

int main() {
    pq::ParkConfig c;

    c = {};  c.visitors = 300;  c.rides = 6;  c.minutes = 180;  c.seed = 7;
    test_config("parque pequeno", c, true);

    c = {};  c.visitors = 1500;  c.rides = 10;  c.minutes = 120;  c.seed = 11;  c.incident_mean = 3;
    test_config("muitas quebras", c, true);

    c = {};  c.visitors = 40;  c.rides = 2;  c.minutes = 90;  c.seed = 3;
    test_config("minusculo", c, false);

    c = {};  c.visitors = 800;  c.rides = 26;  c.minutes = 100;  c.seed = 5;  c.capacity_min = c.capacity_max = 3;
    test_config("26 atracoes", c, true);

    // Pouca capacidade: as filas estouram o limite de 30 min e aparecem recusas.
    c = {};  c.visitors = 1500;  c.rides = 6;  c.minutes = 180;  c.seed = 13;
    c.capacity_min = 5;  c.capacity_max = 12;
    test_config("fila longa (recusas)", c, true, true);

    test_ride_parameters();

    // Decisão com cansaço forte, avaliando todas as atrações e escolha quase determinista.
    c = {};  c.visitors = 600;  c.rides = 10;  c.minutes = 150;  c.seed = 17;
    c.fatigue_per_ride = 0.5;  c.consider = 0;  c.temperature = 2.0;
    test_config("cansaco forte", c, false);

    test_decision_model();
    test_variety();

    std::puts("Todos os testes do simulador passaram.");
    return 0;
}
