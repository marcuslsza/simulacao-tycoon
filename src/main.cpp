// main.cpp
// ETAPA 5 - Linha de comando do experimento.
//
// Para cada combinação (tamanho n, distribuição, fração de reagendamentos):
//   1. gera UMA carga de trabalho;
//   2. roda as três estruturas sobre essa mesma carga (mediana de 'reps');
//   3. confere que os checksums são iguais (corretude);
//   4. imprime uma tabela (e, opcionalmente, um CSV para os gráficos).
//
// Exemplos:
//   ./bench                                           # experimento rápido (padrão)
//   ./bench --sizes 10,100,1000,10000 --dists exp,uniform,bimodal --resched 0,0.1,0.3
//   ./bench --sizes 100000 --skip-list-above 30000 --csv resultados.csv
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "heap_queue.hpp"
#include "hold_bench.hpp"
#include "sorted_array_queue.hpp"
#include "sorted_list_queue.hpp"

namespace {

struct Options {
    std::vector<std::size_t> sizes{10, 100, 1000, 10000, 100000};
    std::vector<pq::Dist> dists{pq::Dist::Exp};
    std::vector<double> resched{0.0};
    std::size_t ops = 100000;            // holds por execução
    int reps = 3;                        // execuções por medida (usa a mediana)
    std::uint64_t seed = 42;
    std::size_t skip_list_above = 30000; // a lista é O(n) por inserção: pula n muito grande
    std::string csv;
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
        if (a == "--sizes") { o.sizes.clear(); for (auto& s : split(next(), ',')) o.sizes.push_back(std::stoull(s)); }
        else if (a == "--dists") { o.dists.clear(); for (auto& s : split(next(), ',')) o.dists.push_back(pq::parse_dist(s)); }
        else if (a == "--resched") { o.resched.clear(); for (auto& s : split(next(), ',')) o.resched.push_back(std::stod(s)); }
        else if (a == "--ops") o.ops = std::stoull(next());
        else if (a == "--reps") o.reps = std::stoi(next());
        else if (a == "--seed") o.seed = std::stoull(next());
        else if (a == "--skip-list-above") o.skip_list_above = std::stoull(next());
        else if (a == "--csv") o.csv = next();
        else { std::fprintf(stderr, "opcao desconhecida: %s\n", a.c_str()); std::exit(2); }
    }
    return o;
}

struct Row { const char* name; pq::Result r; };

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse(argc, argv);

    std::ofstream csv;
    if (!opt.csv.empty()) {
        csv.open(opt.csv);
        csv << "n,dist,resched,estrutura,ns_por_hold\n";
    }

    std::printf("%8s %-8s %7s  %-15s %12s\n", "n", "dist", "resched", "estrutura", "ns/hold");
    for (std::size_t n : opt.sizes) {
        for (pq::Dist d : opt.dists) {
            for (double rs : opt.resched) {
                const pq::Workload w = pq::make_workload(n, opt.ops, d, rs, opt.seed);

                std::vector<Row> rows;
                rows.push_back({pq::IndexedBinaryHeap::name(),
                                pq::measure<pq::IndexedBinaryHeap>(w, n, opt.reps)});
                rows.push_back({pq::SortedArrayQueue::name(),
                                pq::measure<pq::SortedArrayQueue>(w, n, opt.reps)});
                const bool run_list = n <= opt.skip_list_above;
                if (run_list)
                    rows.push_back({pq::SortedLinkedListQueue::name(),
                                    pq::measure<pq::SortedLinkedListQueue>(w, n, opt.reps)});

                bool same = true;
                for (const Row& r : rows) same = same && (r.r.checksum == rows[0].r.checksum);

                for (const Row& r : rows) {
                    std::printf("%8zu %-8s %7.2f  %-15s %12.1f\n", n, pq::dist_name(d), rs, r.name,
                                r.r.ns_per_hold);
                    if (csv.is_open())
                        csv << n << ',' << pq::dist_name(d) << ',' << rs << ',' << r.name << ','
                            << r.r.ns_per_hold << '\n';
                }
                if (!run_list)
                    std::printf("%8zu %-8s %7.2f  %-15s %12s\n", n, pq::dist_name(d), rs,
                                pq::SortedLinkedListQueue::name(), "(pulada)");
                if (!same) {
                    std::fprintf(stderr, "ERRO: checksums diferentes (n=%zu, %s, resched=%.2f)\n", n,
                                 pq::dist_name(d), rs);
                    return 1;
                }
                std::fflush(stdout);
            }
        }
    }
    return 0;
}
