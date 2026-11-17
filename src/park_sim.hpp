// park_sim.hpp
// ETAPA 6 - Simulador de parque dirigido por eventos (discrete-event simulation).
//
// A mesma fila de eventos do benchmark agora é usada "de verdade": o simulador
// é um template sobre Q, então o MESMO código de parque roda com heap, array
// ordenado ou lista. Como a simulação é determinística, as três estruturas
// produzem exatamente a mesma história (mesmo checksum); só o tempo muda.
//
// MODELO
//   * Visitantes alternam: pensar -> caminhar até uma atração -> esperar na
//     fila -> passear -> pensar ...
//   * Cada atração parte a cada 'cycle' minutos levando até 'capacity'
//     visitantes da frente da fila.
//   * A fila tem paciência: se o tempo de espera passa da paciência do
//     visitante, ele desiste.
//   * De vez em quando uma atração QUEBRA (Incident): quem está dentro demora
//     mais para sair, a próxima partida atrasa e a fila perde paciência.
//
// COMO CADA OPERAÇÃO DA FILA DE EVENTOS APARECE NO PARQUE
//   push        - todo novo acontecimento agendado (caminhar, esperar, passear...)
//   pop_min     - o laço principal: próximo evento no tempo
//   cancel      - visitante embarca: seu "perder a paciência" deixa de existir
//   reschedule  - quebra de atração: EndRide e Dispatch ADIAM (chave aumenta) e
//                 GiveUp de quem está na fila ENCURTA (chave diminui), em rajada
//
// INVARIANTE (a mesma da etapa 1): cada entidade tem exatamente UM evento
// pendente, e o estado da entidade diz de que tipo ele é:
//   Idle=Choose, Walking=Arrive, Queued=GiveUp, Riding=EndRide
//   atração=Dispatch, gerador de incidentes=Incident
// Ids: visitantes 0..V-1, atrações V..V+R-1, gerador de incidentes V+R.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <random>
#include <stdexcept>
#include <vector>

#include "event.hpp"

namespace pq {

struct ParkConfig {
    std::size_t visitors = 2000;
    std::size_t rides = 12;      // 1..26 (cada uma vira uma letra no mapa)
    double minutes = 240.0;      // duração simulada
    std::uint64_t seed = 42;
    std::size_t capacity = 0;    // lugares por partida; 0 = automático (ver abaixo)
    double cycle = 2.0;          // minutos entre partidas
    double ride_time = 1.5;      // duração do passeio (precisa ser < cycle)
    double patience_mean = 25.0; // paciência média na fila (exponencial)
    double think_mean = 3.0;     // pausa entre atrações
    double incident_mean = 12.0; // intervalo médio entre quebras no parque todo
    double repair_mean = 8.0;    // atraso médio causado por uma quebra
    double balk_wait = 60.0;     // não entra na fila se a espera estimada passar disso
};

enum class VisitorState : std::uint8_t { Idle, Walking, Queued, Riding };

struct ParkStats {
    std::uint64_t events = 0;      // eventos processados
    std::uint64_t n_push = 0, n_cancel = 0, n_resched = 0;  // operações feitas na fila
    std::uint64_t boarded = 0, completed = 0, giveups = 0, balks = 0, incidents = 0;
    double mean_wait = 0;          // espera média (min) de quem embarcou
    std::size_t peak_pending = 0;  // maior número de eventos pendentes
    std::size_t peak_line = 0;     // maior fila em uma atração
    std::uint64_t checksum = 0;    // hash da ordem de processamento dos eventos
};

template <class Q>
class ParkSim {
public:
    static constexpr int kW = 48, kH = 14;  // dimensões do mapa (só para distância e desenho)

    explicit ParkSim(const ParkConfig& c)
        : c_(c), V_(c.visitors), R_(c.rides), q_(c.visitors + c.rides + 1), rng_(c.seed) {
        if (V_ < 1) throw std::invalid_argument("visitors deve ser >= 1");
        if (R_ < 1 || R_ > 26) throw std::invalid_argument("rides deve estar entre 1 e 26");
        if (c.ride_time >= c.cycle) throw std::invalid_argument("ride_time deve ser < cycle");

        // Capacidade automática: ~metade da demanda sem espera, o que satura as
        // filas e faz paciência/desistência/cancelamento acontecerem de verdade.
        // (demanda por atração ~ V / (R * 7.5 min); oferta = capacity / cycle)
        cap_ = c.capacity ? c.capacity
                          : std::max<std::size_t>(4, static_cast<std::size_t>(std::ceil(
                                static_cast<double>(V_) * c.cycle / (15.0 * static_cast<double>(R_)))));

        st_.assign(V_, VisitorState::Idle);
        target_.assign(V_, 0);
        ticket_.assign(V_, 0);
        join_time_.assign(V_, 0.0);
        loc_.assign(V_, static_cast<std::uint32_t>(R_));  // todos começam na entrada
        line_.assign(R_, {});
        qlen_.assign(R_, 0);
        riding_.assign(R_, 0);
        aboard_.assign(R_, {});
        broken_until_.assign(R_, 0.0);
        evt_time_.assign(V_ + R_ + 1, 0.0);

        layout();

        // Popularidade: a atração A é a mais disputada, Z a menos.
        cum_pop_.resize(R_);
        double sum = 0;
        for (std::size_t r = 0; r < R_; ++r) {
            sum += 1.0 / std::pow(static_cast<double>(r + 1), 0.8);
            cum_pop_[r] = sum;
        }
        for (double& x : cum_pop_) x /= sum;

        // Eventos iniciais (um por entidade, respeitando o invariante).
        for (std::size_t v = 0; v < V_; ++v)
            schedule(static_cast<std::uint32_t>(v), EventType::Choose, 10.0 * u01());  // chegam ao longo de 10 min
        for (std::size_t r = 0; r < R_; ++r)
            schedule(ride_id(r), EventType::Dispatch, c_.cycle * u01());
        schedule(incident_id(), EventType::Incident, expo(c_.incident_mean));
    }

    // Executa até o fim do horizonte. 'frame' (opcional) é chamado a cada
    // 'frame_every' minutos simulados para desenhar o parque; com
    // frame_every == 0 nada é desenhado e o laço fica "limpo" para medir.
    template <class Frame>
    ParkStats run(double frame_every, Frame&& frame) {
        const double until = c_.minutes;
        double next_frame = 0.0;
        Event e{};
        while (q_.pop_min(e)) {
            if (e.time > until) {  // passou do horizonte: devolve o evento e para
                q_.push(e);        // (mantém o estado final consistente: 1 evento por entidade)
                break;
            }
            now_ = e.time;
            ++events_;
            checksum_ = (checksum_ ^ ((static_cast<std::uint64_t>(e.id) << 3) |
                                      static_cast<std::uint64_t>(e.type))) * 1099511628211ull;
            switch (e.type) {
                case EventType::Choose:   on_choose(e.id); break;
                case EventType::Arrive:   on_arrive(e.id); break;
                case EventType::GiveUp:   on_giveup(e.id); break;
                case EventType::Dispatch: on_dispatch(e.id); break;
                case EventType::EndRide:  on_end_ride(e.id); break;
                case EventType::Incident: on_incident(); break;
            }
            peak_pending_ = std::max(peak_pending_, q_.size() + 1);  // +1: o evento atual já saiu
            if (frame_every > 0.0 && now_ >= next_frame) {
                frame(*this);
                next_frame += frame_every;
            }
        }
        return stats();
    }
    ParkStats run() { return run(0.0, [](const ParkSim&) {}); }

    // ---- consultas (usadas pelo desenho do mapa e pelos testes) ----
    const Q& queue() const { return q_; }
    const ParkConfig& config() const { return c_; }
    double now() const { return now_; }
    std::size_t capacity() const { return cap_; }
    std::size_t pending() const { return q_.size(); }
    std::size_t line_length(std::size_t r) const { return qlen_[r]; }
    std::size_t riding(std::size_t r) const { return riding_[r]; }
    bool broken(std::size_t r) const { return now_ < broken_until_[r]; }
    double repair_left(std::size_t r) const { return std::max(0.0, broken_until_[r] - now_); }
    int ride_x(std::size_t r) const { return rx_[r]; }
    int ride_y(std::size_t r) const { return ry_[r]; }
    int entrance_y() const { return ry_[R_]; }
    std::size_t count_state(VisitorState s) const {
        return static_cast<std::size_t>(std::count(st_.begin(), st_.end(), s));
    }

    ParkStats stats() const {
        ParkStats s;
        s.events = events_;
        s.n_push = n_push_; s.n_cancel = n_cancel_; s.n_resched = n_resched_;
        s.boarded = boarded_; s.completed = completed_; s.giveups = giveups_;
        s.balks = balks_; s.incidents = incidents_;
        s.mean_wait = boarded_ ? wait_sum_ / static_cast<double>(boarded_) : 0.0;
        s.peak_pending = peak_pending_; s.peak_line = peak_line_;
        s.checksum = checksum_;
        return s;
    }

private:
    struct Entry { std::uint32_t v; std::uint64_t ticket; };

    ParkConfig c_;
    std::size_t V_, R_, cap_ = 0;
    Q q_;
    std::mt19937_64 rng_;

    // estado dos visitantes
    std::vector<VisitorState> st_;
    std::vector<std::uint32_t> target_;    // atração-alvo (ou atual)
    std::vector<std::uint64_t> ticket_;    // identifica a entrada do visitante na fila (ver on_dispatch)
    std::vector<double> join_time_;        // quando entrou na fila
    std::vector<std::uint32_t> loc_;       // onde está (índice da atração, ou R = entrada)
    // estado das atrações
    std::vector<std::deque<Entry>> line_;  // fila FIFO, com remoção preguiçosa
    std::vector<std::size_t> qlen_;        // tamanho REAL da fila (sem entradas obsoletas)
    std::vector<std::size_t> riding_;
    std::vector<std::vector<std::uint32_t>> aboard_;
    std::vector<double> broken_until_;
    // agenda: instante do evento pendente de cada entidade (a fila não expõe isso)
    std::vector<double> evt_time_;
    // mapa
    std::vector<int> rx_, ry_;
    std::vector<double> cum_pop_;

    double now_ = 0.0;
    std::uint64_t seq_ = 0, ticket_counter_ = 0, checksum_ = 1469598103934665603ull;
    std::uint64_t events_ = 0, n_push_ = 0, n_cancel_ = 0, n_resched_ = 0;
    std::uint64_t boarded_ = 0, completed_ = 0, giveups_ = 0, balks_ = 0, incidents_ = 0;
    double wait_sum_ = 0.0;
    std::size_t peak_pending_ = 0, peak_line_ = 0;

    std::uint32_t ride_id(std::size_t r) const { return static_cast<std::uint32_t>(V_ + r); }
    std::uint32_t incident_id() const { return static_cast<std::uint32_t>(V_ + R_); }

    // ---- aleatoriedade (própria, para não depender de std::*_distribution) ----
    double u01() { return static_cast<double>(rng_() >> 11) * (1.0 / 9007199254740992.0); }
    double expo(double mean) { return -mean * std::log1p(-u01()); }

    // ---- as 3 operações da fila passam por aqui: contam e mantêm a agenda ----
    void schedule(std::uint32_t id, EventType type, double t) {
        q_.push(Event{t, seq_++, id, type});
        evt_time_[id] = t;
        ++n_push_;
    }
    void cancel_event(std::uint32_t id) {
        if (!q_.cancel(id)) throw std::logic_error("cancel de evento inexistente (invariante quebrado)");
        ++n_cancel_;
    }
    void reschedule_event(std::uint32_t id, double t) {
        if (!q_.reschedule(id, t, seq_++)) throw std::logic_error("reschedule de evento inexistente");
        evt_time_[id] = t;
        ++n_resched_;
    }

    // ---- mapa: atrações em grade; a entrada fica à esquerda ----
    void layout() {
        rx_.assign(R_ + 1, 0);
        ry_.assign(R_ + 1, 0);
        const std::size_t cols = std::min<std::size_t>(R_, 6);
        const std::size_t rows = (R_ + cols - 1) / cols;
        for (std::size_t r = 0; r < R_; ++r) {
            const std::size_t c = r % cols, row = r / cols;
            rx_[r] = 6 + (cols > 1 ? static_cast<int>(c) * (kW - 12) / static_cast<int>(cols - 1) : 0);
            ry_[r] = rows > 1 ? 2 + static_cast<int>(row) * (kH - 5) / static_cast<int>(rows - 1) : kH / 2;
        }
        rx_[R_] = 0;
        ry_[R_] = kH / 2;  // entrada
    }
    double walk_time(std::uint32_t from, std::uint32_t to) {
        const int d = std::abs(rx_[from] - rx_[to]) + std::abs(ry_[from] - ry_[to]);
        return 0.2 + 0.04 * d + expo(0.3);
    }

    // Duas candidatas sorteadas pela popularidade; fica com a de menor fila.
    std::uint32_t pick_ride() {
        auto draw = [&]() {
            const double u = u01();
            auto it = std::lower_bound(cum_pop_.begin(), cum_pop_.end(), u);
            return static_cast<std::uint32_t>(std::min<std::size_t>(it - cum_pop_.begin(), R_ - 1));
        };
        const std::uint32_t a = draw(), b = draw();
        return qlen_[b] < qlen_[a] ? b : a;
    }

    // ---- tratadores de evento ----
    void on_choose(std::uint32_t v) {
        const std::uint32_t r = pick_ride();
        target_[v] = r;
        st_[v] = VisitorState::Walking;
        schedule(v, EventType::Arrive, now_ + walk_time(loc_[v], r));
    }

    void on_arrive(std::uint32_t v) {
        const std::uint32_t r = target_[v];
        loc_[v] = r;
        const double est_wait = static_cast<double>(qlen_[r]) / static_cast<double>(cap_) * c_.cycle;
        if (est_wait > c_.balk_wait) {  // fila "absurda": nem entra
            ++balks_;
            st_[v] = VisitorState::Idle;
            schedule(v, EventType::Choose, now_ + expo(c_.think_mean));
            return;
        }
        ticket_[v] = ++ticket_counter_;
        line_[r].push_back({v, ticket_[v]});
        ++qlen_[r];
        peak_line_ = std::max(peak_line_, qlen_[r]);
        st_[v] = VisitorState::Queued;
        join_time_[v] = now_;
        schedule(v, EventType::GiveUp, now_ + expo(c_.patience_mean));
    }

    void on_giveup(std::uint32_t v) {
        --qlen_[target_[v]];  // a entrada na deque fica obsoleta e é descartada depois
        ++giveups_;
        st_[v] = VisitorState::Idle;
        schedule(v, EventType::Choose, now_ + expo(0.5 * c_.think_mean));
    }

    void on_dispatch(std::uint32_t id) {
        const std::size_t r = id - V_;
        aboard_[r].clear();
        std::size_t taken = 0;
        auto& dq = line_[r];
        while (taken < cap_ && !dq.empty()) {
            const Entry en = dq.front();
            dq.pop_front();
            // Remoção preguiçosa: quem desistiu deixou uma entrada obsoleta.
            // O ticket distingue a entrada atual de uma antiga do mesmo visitante.
            if (st_[en.v] != VisitorState::Queued || ticket_[en.v] != en.ticket || target_[en.v] != r)
                continue;
            cancel_event(en.v);  // o GiveUp deste visitante deixa de existir
            --qlen_[r];
            wait_sum_ += now_ - join_time_[en.v];
            ++boarded_;
            st_[en.v] = VisitorState::Riding;
            aboard_[r].push_back(en.v);
            schedule(en.v, EventType::EndRide, now_ + c_.ride_time);
            ++taken;
        }
        riding_[r] = taken;
        schedule(id, EventType::Dispatch, now_ + c_.cycle);
    }

    void on_end_ride(std::uint32_t v) {
        const std::uint32_t r = target_[v];
        if (riding_[r] > 0) --riding_[r];
        ++completed_;
        st_[v] = VisitorState::Idle;
        schedule(v, EventType::Choose, now_ + expo(c_.think_mean));
    }

    // A quebra é a fonte das rajadas de reschedule.
    void on_incident() {
        ++incidents_;
        const std::size_t r = static_cast<std::size_t>(rng_() % R_);
        const double delay = expo(c_.repair_mean);
        broken_until_[r] = std::max(broken_until_[r], now_ + delay);

        // 1) quem está dentro demora mais para sair (chave AUMENTA)
        for (std::uint32_t v : aboard_[r])
            if (st_[v] == VisitorState::Riding && target_[v] == r)
                reschedule_event(v, evt_time_[v] + delay);
        // 2) a próxima partida atrasa (chave AUMENTA)
        reschedule_event(ride_id(r), evt_time_[ride_id(r)] + delay);
        // 3) a fila perde paciência: o tempo restante encolhe (chave DIMINUI)
        for (const Entry& en : line_[r])
            if (st_[en.v] == VisitorState::Queued && ticket_[en.v] == en.ticket && target_[en.v] == r)
                reschedule_event(en.v, now_ + 0.6 * (evt_time_[en.v] - now_));

        schedule(incident_id(), EventType::Incident, now_ + expo(c_.incident_mean));
    }
};

}  // namespace pq
