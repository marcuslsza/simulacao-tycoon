// park_main.cpp - benchmark do simulador de parque.
//
// Simula o mesmo parque com as três filas de eventos, mede o tempo e CONFERE que
// as três geraram exatamente a mesma história (checksum e contadores iguais).
// Só o tempo pode diferir.
//
//   ./park                                        # 2000 visitantes, três filas
//   ./park --visitors 10000 --minutes 120
//   ./park --visitors 20000 --queue heap,array    # escolhe as estruturas
//   ./park --visitors 30 --rides 4 --minutes 1200 # parque pequeno (n baixo)
//   ./park --capacity-range 20,60 --ride-range 2,10 --balk-wait 30
//   ./park --repeat-penalty 15                    # repetir a atração só "custa" 15 min de fila
//   ./park --repeat-penalty 0                     # sem variedade
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "cli.hpp"
#include "heap_queue.hpp"
#include "park_sim.hpp"
#include "sorted_array_queue.hpp"
#include "sorted_list_queue.hpp"

namespace {

struct Options {
    pq::ParkConfig cfg;
    std::vector<std::string> queues{"heap", "array", "list"};
    int reps = 3;
    std::size_t skip_list_above = 30000;
};

Options parse(int argc, char** argv) {
    Options o;
    pq::ParkConfig& c = o.cfg;
    using Setter = std::function<void(const std::string&)>;
    auto num = [](auto& dst) -> Setter {  // opção com um número
        return [&dst](const std::string& s) { dst = cli::to_num<std::decay_t<decltype(dst)>>(s); };
    };
    auto range = [](auto& lo, auto& hi, std::string flag) -> Setter {  // opção MIN,MAX
        return [&lo, &hi, flag](const std::string& s) {
            const auto p = cli::split(s);
            if (p.size() != 2) { std::fprintf(stderr, "use %s MIN,MAX\n", flag.c_str()); std::exit(2); }
            lo = cli::to_num<std::decay_t<decltype(lo)>>(p[0]);
            hi = cli::to_num<std::decay_t<decltype(hi)>>(p[1]);
        };
    };
    const std::map<std::string, Setter> flags = {
        {"--visitors", num(c.visitors)},   {"--rides", num(c.rides)},       {"--minutes", num(c.minutes)},
        {"--seed", num(c.seed)},           {"--load-time", num(c.load_time)}, {"--balk-wait", num(c.balk_wait)},
        {"--patience", num(c.patience_mean)}, {"--incident-mean", num(c.incident_mean)},
        {"--capacity", [&](const std::string& s) { c.capacity_min = c.capacity_max = cli::to_num<std::size_t>(s); }},
        {"--capacity-range", range(c.capacity_min, c.capacity_max, "--capacity-range")},
        {"--ride-range", range(c.ride_min, c.ride_max, "--ride-range")},
        {"--attr-range", range(c.attr_min, c.attr_max, "--attr-range")},
        // decisão do visitante
        {"--w-wait", num(c.w_wait)},       {"--w-dist", num(c.w_dist)},     {"--temperature", num(c.temperature)},
        {"--consider", num(c.consider)},   {"--fatigue-per-ride", num(c.fatigue_per_ride)},
        {"--fatigue-tau", num(c.fatigue_tau)}, {"--fatigue-sens", num(c.fatigue_sens)},
        {"--rest-base", num(c.rest_base)}, {"--rest-gain", num(c.rest_gain)}, {"--rest-mean", num(c.rest_mean)},
        {"--rest-tau", num(c.rest_tau)},   {"--repeat-penalty", num(c.repeat_penalty)},  // inf, 0 ou N
        // medição
        {"--queue", [&](const std::string& s) { o.queues = cli::split(s); }},
        {"--reps", num(o.reps)},           {"--skip-list-above", num(o.skip_list_above)},
    };
    cli::parse(argc, argv, [&](const std::string& a, auto&& next) {
        const auto it = flags.find(a);
        if (it == flags.end()) return false;
        it->second(next());
        return true;
    });
    return o;
}

struct Row {
    std::string name;
    double ns_per_event = 0;
    double total_ms = 0;
    pq::ParkStats st;
};

template <class Q>
Row bench_one(const Options& o) {
    std::vector<double> total_ns;
    pq::ParkStats st;
    for (int r = 0; r < o.reps; ++r) {
        pq::ParkSim<Q> sim(o.cfg);  // montagem (eventos iniciais) fora do cronômetro
        const auto t0 = std::chrono::steady_clock::now();
        st = sim.run();
        const auto t1 = std::chrono::steady_clock::now();
        total_ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    std::sort(total_ns.begin(), total_ns.end());
    const double med = total_ns[total_ns.size() / 2];
    return {Q::name(), med / static_cast<double>(st.events), med / 1e6, st};
}

std::string repeat_label(double p) {
    if (std::isinf(p)) return "nunca em sequencia";
    if (p == 0.0) return "livre";
    char b[48];
    std::snprintf(b, sizeof b, "penalidade %.1f min", p);
    return b;
}

unsigned long long ull(std::uint64_t x) { return x; }  // para o printf("%llu")

void print_report(const Options& o, const std::vector<Row>& rows) {
    const pq::ParkConfig& c = o.cfg;
    const pq::ParkStats& s = rows[0].st;  // a história é a mesma nas três filas
    std::printf("Parque: %zu visitantes, %zu atracoes, %.0f min simulados, semente %llu\n", c.visitors, c.rides,
                c.minutes, ull(c.seed));
    std::printf("Atracoes: capacidade %zu a %zu por partida | passeios de %.1f a %.1f min | "
                "recusa fila com espera estimada > %.0f min | paciencia media %.0f min\n",
                s.cap_min, s.cap_max, s.dur_min, s.dur_max, c.balk_wait, c.patience_mean);
    std::printf("Decisao: atratividade %.0f a %.0f | peso espera %.1f | peso caminhada %.1f | "
                "avalia %zu atracoes | cansaco +%.2f por passeio | repetir atracao: %s\n",
                c.attr_min, c.attr_max, c.w_wait, c.w_dist, c.consider, c.fatigue_per_ride,
                repeat_label(c.repeat_penalty).c_str());
    std::printf("Eventos: %llu | push %llu | cancel %llu | reschedule %llu | pico pendente %zu\n", ull(s.events),
                ull(s.n_push), ull(s.n_cancel), ull(s.n_resched), s.peak_pending);
    std::printf("Embarques %llu | desistencias %llu | recusaram %llu | descansos %llu | quebras %llu | "
                "espera media %.1f min | maior fila %zu\n",
                ull(s.boarded), ull(s.giveups), ull(s.balks), ull(s.rests), ull(s.incidents), s.mean_wait, s.peak_line);
    std::printf("Passeios por visitante: %.1f | mesma atracao em sequencia: %llu (%.1f%% dos passeios) | "
                "cansaco medio nas decisoes: %.2f\n\n",
                static_cast<double>(s.completed) / static_cast<double>(c.visitors), ull(s.repeats),
                s.completed ? 100.0 * static_cast<double>(s.repeats) / static_cast<double>(s.completed) : 0.0,
                s.mean_fatigue);

    std::printf("%-16s %12s %14s\n", "estrutura", "ns/evento", "tempo (ms)");
    for (const Row& r : rows) std::printf("%-16s %12.1f %14.1f\n", r.name.c_str(), r.ns_per_event, r.total_ms);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options o = parse(argc, argv);

        std::vector<Row> rows;
        for (const std::string& q : o.queues) {
            if (q == "heap") rows.push_back(bench_one<pq::IndexedBinaryHeap>(o));
            else if (q == "array") rows.push_back(bench_one<pq::SortedArrayQueue>(o));
            else if (q == "list") {
                if (o.cfg.visitors > o.skip_list_above) {
                    std::printf("(lista pulada: visitantes > %zu; use --skip-list-above para forcar)\n", o.skip_list_above);
                    continue;
                }
                rows.push_back(bench_one<pq::SortedLinkedListQueue>(o));
            } else {
                std::fprintf(stderr, "fila desconhecida: %s (use heap, array, list)\n", q.c_str());
                return 2;
            }
        }
        if (rows.empty()) return 0;

        print_report(o, rows);
        const bool ok = std::all_of(rows.begin(), rows.end(),
                                    [&](const Row& r) { return pq::same_history(r.st, rows[0].st); });
        std::printf("\nMesma historia em todas as estruturas: %s\n", ok ? "OK" : "DIFERENTE (bug!)");
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 2;
    }
}
