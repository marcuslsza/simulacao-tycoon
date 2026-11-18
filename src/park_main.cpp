// park_main.cpp
// ETAPA 6 (CLI) - Roda o simulador de parque de duas formas:
//
//   1) MODO BENCHMARK (padrão): simula o mesmo parque com as três filas de
//      eventos, mede o tempo, e CONFERE que as três geraram exatamente a mesma
//      história (checksum e contadores iguais). Só o tempo pode diferir.
//
//   2) MODO DEMO (--demo): desenha o parque no terminal a cada N minutos
//      simulados (mapa em ASCII, filas, atrações quebradas) usando UMA fila.
//
// Exemplos:
//   ./park                                        # benchmark padrão (2000 visitantes)
//   ./park --visitors 10000 --minutes 120
//   ./park --visitors 20000 --queue heap,array    # escolhe as estruturas
//   ./park --demo --visitors 1500 --minutes 120 --frame-every 10
//   ./park --demo --delay-ms 400 --clear          # "animação" no terminal
//   ./park --capacity-range 20,60 --ride-range 2,10 --balk-wait 30
//   ./park --attr-range 10,40 --w-dist 3 --fatigue-per-ride 0.25 --rest-gain 40
//   ./park --repeat-penalty 15      # repetir a mesma atração só "custa" 15 min de fila
//   ./park --repeat-penalty 0       # sem variedade (como na versão anterior)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

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
    bool demo = false;
    double frame_every = 30.0;
    int delay_ms = 0;
    bool clear = false;
};

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "faltou valor para %s\n", a.c_str()); std::exit(2); }
            return argv[++i];
        };
        if (a == "--visitors") o.cfg.visitors = std::stoull(next());
        else if (a == "--rides") o.cfg.rides = std::stoull(next());
        else if (a == "--minutes") o.cfg.minutes = std::stod(next());
        else if (a == "--seed") o.cfg.seed = std::stoull(next());
        else if (a == "--capacity") {  // todas as atrações com a mesma capacidade
            o.cfg.capacity_min = o.cfg.capacity_max = std::stoull(next());
        } else if (a == "--capacity-range") {  // MIN,MAX sorteado por atração (fixo depois)
            const auto p = split(next(), ',');
            if (p.size() != 2) { std::fprintf(stderr, "use --capacity-range MIN,MAX\n"); std::exit(2); }
            o.cfg.capacity_min = std::stoull(p[0]);
            o.cfg.capacity_max = std::stoull(p[1]);
        } else if (a == "--ride-range") {  // MIN,MAX minutos, sorteado por atração (fixo depois)
            const auto p = split(next(), ',');
            if (p.size() != 2) { std::fprintf(stderr, "use --ride-range MIN,MAX\n"); std::exit(2); }
            o.cfg.ride_min = std::stod(p[0]);
            o.cfg.ride_max = std::stod(p[1]);
        }
        else if (a == "--load-time") o.cfg.load_time = std::stod(next());
        else if (a == "--balk-wait") o.cfg.balk_wait = std::stod(next());
        else if (a == "--attr-range") {  // atratividade fixa por atração, sorteada em MIN,MAX
            const auto p = split(next(), ',');
            if (p.size() != 2) { std::fprintf(stderr, "use --attr-range MIN,MAX\n"); std::exit(2); }
            o.cfg.attr_min = std::stod(p[0]);
            o.cfg.attr_max = std::stod(p[1]);
        }
        else if (a == "--w-wait") o.cfg.w_wait = std::stod(next());
        else if (a == "--w-dist") o.cfg.w_dist = std::stod(next());
        else if (a == "--temperature") o.cfg.temperature = std::stod(next());
        else if (a == "--consider") o.cfg.consider = std::stoull(next());
        else if (a == "--fatigue-per-ride") o.cfg.fatigue_per_ride = std::stod(next());
        else if (a == "--fatigue-tau") o.cfg.fatigue_tau = std::stod(next());
        else if (a == "--fatigue-sens") o.cfg.fatigue_sens = std::stod(next());
        else if (a == "--rest-base") o.cfg.rest_base = std::stod(next());
        else if (a == "--rest-gain") o.cfg.rest_gain = std::stod(next());
        else if (a == "--rest-mean") o.cfg.rest_mean = std::stod(next());
        else if (a == "--rest-tau") o.cfg.rest_tau = std::stod(next());
        else if (a == "--repeat-penalty") o.cfg.repeat_penalty = std::stod(next());  // inf, 0 ou N
        else if (a == "--incident-mean") o.cfg.incident_mean = std::stod(next());
        else if (a == "--patience") o.cfg.patience_mean = std::stod(next());
        else if (a == "--queue") o.queues = split(next(), ',');
        else if (a == "--reps") o.reps = std::stoi(next());
        else if (a == "--skip-list-above") o.skip_list_above = std::stoull(next());
        else if (a == "--demo") o.demo = true;
        else if (a == "--frame-every") o.frame_every = std::stod(next());
        else if (a == "--delay-ms") o.delay_ms = std::stoi(next());
        else if (a == "--clear") o.clear = true;
        else { std::fprintf(stderr, "opcao desconhecida: %s\n", a.c_str()); std::exit(2); }
    }
    return o;
}

// ---------- desenho do parque (modo demo) ----------
template <class Q>
void render(const pq::ParkSim<Q>& s, const char* qname, bool clear) {
    constexpr int W = pq::ParkSim<Q>::kW, H = pq::ParkSim<Q>::kH;
    const std::size_t R = s.config().rides;

    std::vector<std::string> g(H, std::string(W, ' '));
    const int mid = s.entrance_y();
    for (int x = 0; x < W; ++x) g[mid][x] = '.';            // caminho principal
    for (std::size_t r = 0; r < R; ++r) {                   // ramais até cada atração
        const int x = s.ride_x(r), y = s.ride_y(r);
        for (int yy = std::min(y, mid); yy <= std::max(y, mid); ++yy) g[yy][x] = '.';
    }
    for (std::size_t r = 0; r < R; ++r) {                   // atração + tamanho da fila
        const int x = s.ride_x(r), y = s.ride_y(r);
        char label[16];
        std::snprintf(label, sizeof label, "%c:%zu", static_cast<char>('A' + r), s.line_length(r));
        for (int k = 0; label[k] != '\0' && x + k < W; ++k) g[y][x + k] = label[k];
    }
    g[mid][0] = '>';

    if (clear) std::printf("\033[H\033[2J");
    std::printf("t = %6.1f min | visitantes %zu | eventos pendentes %zu | fila: %s\n", s.now(),
                s.config().visitors, s.pending(), qname);
    std::printf("+%s+\n", std::string(W, '-').c_str());
    for (const auto& row : g) std::printf("|%s|\n", row.c_str());
    std::printf("+%s+\n", std::string(W, '-').c_str());

    std::size_t maxq = 1;
    for (std::size_t r = 0; r < R; ++r) maxq = std::max(maxq, s.line_length(r));
    for (std::size_t r = 0; r < R; ++r) {
        const std::size_t bar = s.line_length(r) * 30 / maxq;
        std::printf(" %c %-30s %5zu na fila, %2zu no passeio (cap %zu, %.1f min, atrat. %.0f)%s",
                    static_cast<char>('A' + r), std::string(bar, '#').c_str(), s.line_length(r),
                    s.riding(r), s.capacity(r), s.ride_duration(r), s.attractiveness(r),
                    s.broken(r) ? "" : "\n");
        if (s.broken(r)) std::printf("  [QUEBRADA, ~%.1f min]\n", s.repair_left(r));
    }
    const pq::ParkStats st = s.stats();
    std::printf(" embarques %llu | desistencias %llu | recusaram a fila %llu | descansos %llu | espera media %.1f min | quebras %llu\n\n",
                static_cast<unsigned long long>(st.boarded), static_cast<unsigned long long>(st.giveups),
                static_cast<unsigned long long>(st.balks), static_cast<unsigned long long>(st.rests),
                st.mean_wait, static_cast<unsigned long long>(st.incidents));
    std::fflush(stdout);
}

template <class Q>
int run_demo(const Options& o) {
    pq::ParkSim<Q> sim(o.cfg);
    auto frame = [&](const pq::ParkSim<Q>& s) {
        render(s, Q::name(), o.clear);
        if (o.delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(o.delay_ms));
    };
    const pq::ParkStats st = sim.run(o.frame_every, frame);
    std::printf("Fim. %llu eventos processados (push %llu, cancel %llu, reschedule %llu); "
                "pico de %zu eventos pendentes.\n",
                static_cast<unsigned long long>(st.events), static_cast<unsigned long long>(st.n_push),
                static_cast<unsigned long long>(st.n_cancel), static_cast<unsigned long long>(st.n_resched),
                st.peak_pending);
    return 0;
}

// ---------- benchmark ----------
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
    Row row;
    row.name = Q::name();
    row.total_ms = med / 1e6;
    row.ns_per_event = med / static_cast<double>(st.events);
    row.st = st;
    return row;
}

std::string repeat_label(double p) {
    if (std::isinf(p)) return "nunca em sequencia";
    if (p == 0.0) return "livre";
    char b[48];
    std::snprintf(b, sizeof b, "penalidade %.1f min", p);
    return b;
}

bool same_history(const pq::ParkStats& a, const pq::ParkStats& b) {
    return a.checksum == b.checksum && a.events == b.events && a.n_push == b.n_push &&
           a.n_cancel == b.n_cancel && a.n_resched == b.n_resched && a.boarded == b.boarded &&
           a.giveups == b.giveups && a.balks == b.balks && a.completed == b.completed &&
           a.incidents == b.incidents && a.mean_wait == b.mean_wait && a.rests == b.rests &&
           a.repeats == b.repeats && a.mean_fatigue == b.mean_fatigue &&
           a.peak_pending == b.peak_pending && a.peak_line == b.peak_line;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    try {
        o = parse(argc, argv);
        if (o.demo) {
            const std::string q = o.queues.empty() ? "heap" : o.queues[0];
            if (q == "array") return run_demo<pq::SortedArrayQueue>(o);
            if (q == "list") return run_demo<pq::SortedLinkedListQueue>(o);
            return run_demo<pq::IndexedBinaryHeap>(o);
        }

        std::vector<Row> rows;
        for (const std::string& q : o.queues) {
            if (q == "heap") rows.push_back(bench_one<pq::IndexedBinaryHeap>(o));
            else if (q == "array") rows.push_back(bench_one<pq::SortedArrayQueue>(o));
            else if (q == "list") {
                if (o.cfg.visitors > o.skip_list_above) {
                    std::printf("(lista pulada: visitantes > %zu; use --skip-list-above para forcar)\n",
                                o.skip_list_above);
                    continue;
                }
                rows.push_back(bench_one<pq::SortedLinkedListQueue>(o));
            } else {
                std::fprintf(stderr, "fila desconhecida: %s (use heap, array, list)\n", q.c_str());
                return 2;
            }
        }
        if (rows.empty()) return 0;

        const pq::ParkStats& s = rows[0].st;
        std::printf("Parque: %zu visitantes, %zu atracoes, %.0f min simulados, semente %llu\n",
                    o.cfg.visitors, o.cfg.rides, o.cfg.minutes,
                    static_cast<unsigned long long>(o.cfg.seed));
        std::printf("Atracoes: capacidade %zu a %zu por partida | passeios de %.1f a %.1f min | "
                    "recusa fila com espera estimada > %.0f min | paciencia media %.0f min\n",
                    s.cap_min, s.cap_max, s.dur_min, s.dur_max, o.cfg.balk_wait,
                    o.cfg.patience_mean);
        std::printf("Decisao: atratividade %.0f a %.0f | peso espera %.1f | peso caminhada %.1f | "
                    "avalia %zu atracoes | cansaco +%.2f por passeio | repetir atracao: %s\n",
                    o.cfg.attr_min, o.cfg.attr_max, o.cfg.w_wait, o.cfg.w_dist, o.cfg.consider,
                    o.cfg.fatigue_per_ride, repeat_label(o.cfg.repeat_penalty).c_str());
        std::printf("Eventos: %llu | push %llu | cancel %llu | reschedule %llu | pico pendente %zu\n",
                    static_cast<unsigned long long>(s.events), static_cast<unsigned long long>(s.n_push),
                    static_cast<unsigned long long>(s.n_cancel), static_cast<unsigned long long>(s.n_resched),
                    s.peak_pending);
        std::printf("Embarques %llu | desistencias %llu | recusaram %llu | descansos %llu | quebras %llu | espera media %.1f min | maior fila %zu\n",
                    static_cast<unsigned long long>(s.boarded), static_cast<unsigned long long>(s.giveups),
                    static_cast<unsigned long long>(s.balks), static_cast<unsigned long long>(s.rests),
                    static_cast<unsigned long long>(s.incidents), s.mean_wait, s.peak_line);
        std::printf("Passeios por visitante: %.1f | mesma atracao em sequencia: %llu (%.1f%% dos passeios) | "
                    "cansaco medio nas decisoes: %.2f\n\n",
                    static_cast<double>(s.completed) / static_cast<double>(o.cfg.visitors),
                    static_cast<unsigned long long>(s.repeats),
                    s.completed ? 100.0 * static_cast<double>(s.repeats) / static_cast<double>(s.completed) : 0.0,
                    s.mean_fatigue);

        std::printf("%-16s %12s %14s\n", "estrutura", "ns/evento", "tempo (ms)");
        for (const Row& r : rows)
            std::printf("%-16s %12.1f %14.1f\n", r.name.c_str(), r.ns_per_event, r.total_ms);

        bool ok = true;
        for (const Row& r : rows) ok = ok && same_history(r.st, rows[0].st);
        std::printf("\nMesma historia em todas as estruturas: %s\n", ok ? "OK" : "DIFERENTE (bug!)");
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 2;
    }
}
