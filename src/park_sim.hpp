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
//   * Cada atração tem DURAÇÃO (2 a 10 min) e CAPACIDADE máxima próprias,
//     sorteadas uma única vez no início e fixas durante a simulação (uma
//     montanha-russa é rápida e pequena; uma roda-gigante é lenta e grande).
//     Ela parte a cada (duração + tempo de embarque) minutos, levando até
//     'capacidade' visitantes da frente da fila.
//   * DECISÃO DO VISITANTE (modelo de escolha por utilidade, ver on_choose):
//       U(atração) = atratividade - m * ( w_wait * espera_estimada
//                                       + w_dist * tempo_de_caminhada )
//                      - repeat_penalty   (só na atração em que acabou de andar)
//       U(descansar) = rest_base + rest_gain * cansaço
//     O termo de VARIEDADE faz o visitante evitar andar duas vezes seguidas na
//     mesma atração (padrão: proibido; ver ParkConfig::repeat_penalty).
//     O visitante avalia K atrações sorteadas, mais a opção "descansar", e
//     escolhe por sorteio proporcional a exp(U / temperatura) (logit). O
//     cansaço sobe a cada passeio e decai com o tempo; ele (a) torna o
//     descanso mais atraente e (b) aumenta m = 1 + fatigue_sens * cansaço,
//     isto é, o visitante cansado tolera menos fila e menos caminhada.
//   * A fila tem paciência: se o tempo de espera passa da paciência do
//     visitante, ele desiste. E quem chega e vê uma fila com espera estimada
//     acima de 'balk_wait' (30 min) nem entra: recusa a fila.
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
#include <limits>
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
    // Capacidade máxima de embarque: cada atração recebe um valor inteiro
    // sorteado em [capacity_min, capacity_max] e FIXO daí em diante.
    // 0,0 = automático (faixa de 0.5x a 1.5x de uma capacidade-base, ver abaixo).
    // Use min == max para todas as atrações iguais.
    std::size_t capacity_min = 0, capacity_max = 0;
    // Duração do passeio: cada atração recebe um valor sorteado em
    // [ride_min, ride_max] minutos e FIXO daí em diante.
    double ride_min = 2.0, ride_max = 10.0;
    double load_time = 1.0;      // embarque/desembarque entre duas partidas (min)
    double patience_mean = 25.0; // paciência média na fila (exponencial)
    double think_mean = 3.0;     // pausa entre atrações
    double incident_mean = 12.0; // intervalo médio entre quebras no parque todo
    double repair_mean = 8.0;    // atraso médio causado por uma quebra
    double balk_wait = 30.0;     // não entra na fila se a espera estimada passar disso (min)

    // ---- decisão do visitante (utilidades em "minutos equivalentes de espera") ----
    // Atratividade: valor FIXO por atração, sorteado uma vez em [attr_min, attr_max]
    // = quantos minutos de fila o visitante toleraria para andar nela.
    double attr_min = 10.0, attr_max = 40.0;
    double w_wait = 1.0;         // peso da espera estimada na fila
    double w_dist = 3.0;         // peso do tempo de caminhada (1 min andando "custa" 3 min de fila)
    double temperature = 5.0;    // aleatoriedade da escolha: menor = mais determinista
    std::size_t consider = 6;    // quantas atrações cada visitante avalia por decisão (0 = todas)
    // Cansaço: sobe fatigue_per_ride a cada passeio e decai com constante de tempo
    // fatigue_tau (min). Descansar derruba o cansaço rapidamente (rest_tau).
    double fatigue_per_ride = 0.25;
    double fatigue_tau = 45.0;
    double fatigue_sens = 1.0;   // quanto o cansaço amplifica o peso de espera e caminhada
    double rest_base = -10.0;    // utilidade de descansar quando descansado (-1000 desliga o descanso)
    double rest_gain = 40.0;     // quanto cada unidade de cansaço aumenta essa utilidade
    double rest_mean = 8.0;      // duração média de um descanso (min)
    double rest_tau = 6.0;       // recuperação durante o descanso (min)
    // Variedade: penalidade (em minutos equivalentes de espera) descontada da utilidade
    // da atração em que o visitante ACABOU de andar (o último passeio concluído; recusas
    // e desistências não contam, pois não houve passeio). Não é multiplicada pelo cansaço.
    //   infinito (padrão) = nunca anda duas vezes seguidas na mesma atração
    //   0                 = desliga (comportamento da versão anterior)
    //   valor finito      = só PREFERE variar (ex.: 15 = repetir "custa" 15 min de fila)
    // Sem efeito com 1 única atração (não há alternativa).
    double repeat_penalty = std::numeric_limits<double>::infinity();
};

enum class VisitorState : std::uint8_t { Idle, Walking, Queued, Riding };

struct ParkStats {
    std::uint64_t events = 0;      // eventos processados
    std::uint64_t n_push = 0, n_cancel = 0, n_resched = 0;  // operações feitas na fila
    std::uint64_t boarded = 0, completed = 0, giveups = 0, balks = 0, incidents = 0;
    double mean_wait = 0;          // espera média (min) de quem embarcou
    std::uint64_t rests = 0;       // vezes em que o visitante decidiu descansar
    std::uint64_t repeats = 0;     // passeios na MESMA atração do passeio anterior do visitante
    double mean_fatigue = 0;       // cansaço médio no momento das decisões
    std::size_t peak_pending = 0;  // maior número de eventos pendentes
    std::size_t peak_line = 0;     // maior fila em uma atração
    std::uint64_t checksum = 0;    // hash da ordem de processamento dos eventos
    // configuração efetiva sorteada para as atrações (para os relatórios)
    std::size_t cap_min = 0, cap_max = 0;
    double dur_min = 0, dur_max = 0;
};

template <class Q>
class ParkSim {
public:
    static constexpr int kW = 48, kH = 14;  // dimensões do mapa (só para distância e desenho)

    explicit ParkSim(const ParkConfig& c)
        : c_(c), V_(c.visitors), R_(c.rides), q_(c.visitors + c.rides + 1), rng_(c.seed) {
        if (V_ < 1) throw std::invalid_argument("visitors deve ser >= 1");
        if (R_ < 1 || R_ > 26) throw std::invalid_argument("rides deve estar entre 1 e 26");
        if (c.ride_min <= 0.0 || c.ride_max < c.ride_min)
            throw std::invalid_argument("duracao do passeio: use 0 < ride_min <= ride_max");
        if (c.load_time < 0.0) throw std::invalid_argument("load_time deve ser >= 0");
        if (c.attr_max < c.attr_min) throw std::invalid_argument("attr_max deve ser >= attr_min");
        if (c.temperature <= 0.0) throw std::invalid_argument("temperature deve ser > 0");
        if (c.fatigue_tau <= 0.0 || c.rest_tau <= 0.0 || c.rest_mean <= 0.0)
            throw std::invalid_argument("fatigue_tau, rest_tau e rest_mean devem ser > 0");
        if (c.capacity_min > c.capacity_max)
            throw std::invalid_argument("capacity_min deve ser <= capacity_max");
        if (!(c.repeat_penalty >= 0.0))  // também rejeita NaN; infinito é permitido
            throw std::invalid_argument("repeat_penalty deve ser >= 0 (inf = proibido repetir)");

        st_.assign(V_, VisitorState::Idle);
        target_.assign(V_, 0);
        ticket_.assign(V_, 0);
        join_time_.assign(V_, 0.0);
        loc_.assign(V_, static_cast<std::uint32_t>(R_));  // todos começam na entrada
        last_ride_.assign(V_, static_cast<std::uint32_t>(R_));  // R = ainda não andou em nada
        line_.assign(R_, {});
        qlen_.assign(R_, 0);
        riding_.assign(R_, 0);
        aboard_.assign(R_, {});
        broken_until_.assign(R_, 0.0);
        evt_time_.assign(V_ + R_ + 1, 0.0);
        fat_.assign(V_, 0.0);
        fat_t_.assign(V_, 0.0);
        perm_.resize(R_);
        for (std::size_t r = 0; r < R_; ++r) perm_[r] = r;
        util_.assign(R_ + 1, 0.0);

        layout();
        draw_ride_parameters();

        // Eventos iniciais (um por entidade, respeitando o invariante).
        for (std::size_t v = 0; v < V_; ++v)
            schedule(static_cast<std::uint32_t>(v), EventType::Choose, 10.0 * u01());  // chegam ao longo de 10 min
        for (std::size_t r = 0; r < R_; ++r)
            schedule(ride_id(r), EventType::Dispatch, cyc_[r] * u01());
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
    std::size_t capacity(std::size_t r) const { return cap_[r]; }       // lugares por partida
    double ride_duration(std::size_t r) const { return dur_[r]; }       // minutos de passeio
    double attractiveness(std::size_t r) const { return attr_[r]; }
    // Cansaço do visitante v neste instante (já com a recuperação natural aplicada).
    double fatigue(std::uint32_t v) const { return fatigue_now(v); }
    // Última atração em que o visitante v andou (R = ainda não andou em nenhuma).
    std::size_t last_ride(std::uint32_t v) const { return last_ride_[v]; }
    // Utilidade de a atração r para o visitante v, com o estado atual do parque.
    double utility(std::uint32_t v, std::size_t r) const {
        return utility_of(v, r, 1.0 + c_.fatigue_sens * fatigue_now(v));
    }
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
        s.balks = balks_; s.incidents = incidents_; s.rests = rests_; s.repeats = repeats_;
        s.mean_fatigue = choices_ ? fatigue_sum_ / static_cast<double>(choices_) : 0.0;
        s.mean_wait = boarded_ ? wait_sum_ / static_cast<double>(boarded_) : 0.0;
        s.peak_pending = peak_pending_; s.peak_line = peak_line_;
        s.checksum = checksum_;
        s.cap_min = *std::min_element(cap_.begin(), cap_.end());
        s.cap_max = *std::max_element(cap_.begin(), cap_.end());
        s.dur_min = *std::min_element(dur_.begin(), dur_.end());
        s.dur_max = *std::max_element(dur_.begin(), dur_.end());
        return s;
    }

private:
    struct Entry { std::uint32_t v; std::uint64_t ticket; };

    ParkConfig c_;
    std::size_t V_, R_;
    Q q_;
    std::mt19937_64 rng_;

    // estado dos visitantes
    std::vector<VisitorState> st_;
    std::vector<std::uint32_t> target_;    // atração-alvo (ou atual)
    std::vector<std::uint64_t> ticket_;    // identifica a entrada do visitante na fila (ver on_dispatch)
    std::vector<double> join_time_;        // quando entrou na fila
    std::vector<std::uint32_t> loc_;       // onde está (índice da atração, ou R = entrada)
    std::vector<std::uint32_t> last_ride_; // última atração em que ANDOU (R = nenhuma); difere de loc_,
                                           // que também muda quando o visitante recusa uma fila
    // estado das atrações
    std::vector<std::deque<Entry>> line_;  // fila FIFO, com remoção preguiçosa
    std::vector<std::size_t> cap_;         // capacidade máxima de cada atração (fixa)
    std::vector<double> dur_;              // duração do passeio de cada atração (fixa)
    std::vector<double> cyc_;              // intervalo entre partidas = dur_ + load_time
    std::vector<double> attr_;             // atratividade de cada atração (fixa)
    std::vector<std::size_t> qlen_;        // tamanho REAL da fila (sem entradas obsoletas)
    std::vector<std::size_t> riding_;
    std::vector<std::vector<std::uint32_t>> aboard_;
    std::vector<double> broken_until_;
    // agenda: instante do evento pendente de cada entidade (a fila não expõe isso)
    std::vector<double> evt_time_;
    // mapa
    std::vector<int> rx_, ry_;
    // cansaço de cada visitante: valor em fat_t_ (instante da última atualização)
    std::vector<double> fat_, fat_t_;
    // área de trabalho da decisão (reutilizada, sem alocação por decisão)
    std::vector<std::size_t> perm_;
    std::vector<double> util_;

    double now_ = 0.0;
    std::uint64_t seq_ = 0, ticket_counter_ = 0, checksum_ = 1469598103934665603ull;
    std::uint64_t events_ = 0, n_push_ = 0, n_cancel_ = 0, n_resched_ = 0;
    std::uint64_t boarded_ = 0, completed_ = 0, giveups_ = 0, balks_ = 0, incidents_ = 0;
    std::uint64_t rests_ = 0, choices_ = 0, repeats_ = 0;
    double fatigue_sum_ = 0.0;
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
    // Sorteia UMA vez a duração e a capacidade de cada atração (determinístico
    // pela semente; roda antes dos eventos iniciais, então é igual em todas as filas).
    void draw_ride_parameters() {
        std::size_t cmin = c_.capacity_min, cmax = c_.capacity_max;
        if (cmin == 0 && cmax == 0) {
            // Capacidade-base automática: ~metade da demanda, para as filas
            // saturarem e paciência, recusa e cancelamento acontecerem de verdade.
            //   ciclo do visitante sem fila ~ pensar + caminhar (~2) + passeio + meia espera
            //   demanda por atração  = V / (R * ciclo do visitante)
            //   oferta por atração   = capacidade / (passeio + embarque)
            const double dm = 0.5 * (c_.ride_min + c_.ride_max);
            const double visitor_cycle = c_.think_mean + 2.0 + dm + 0.5 * (dm + c_.load_time);
            const double base = 0.5 * static_cast<double>(V_) / static_cast<double>(R_) *
                                (dm + c_.load_time) / visitor_cycle;
            cmin = std::max<std::size_t>(4, static_cast<std::size_t>(std::llround(0.5 * base)));
            cmax = std::max<std::size_t>(cmin, static_cast<std::size_t>(std::llround(1.5 * base)));
        } else if (cmin == 0) {
            cmin = 1;
        }
        cap_.assign(R_, 0);
        dur_.assign(R_, 0.0);
        cyc_.assign(R_, 0.0);
        attr_.assign(R_, 0.0);
        for (std::size_t r = 0; r < R_; ++r) {
            dur_[r] = c_.ride_min + (c_.ride_max - c_.ride_min) * u01();
            cap_[r] = cmin + static_cast<std::size_t>(rng_() % (cmax - cmin + 1));
            cyc_[r] = dur_[r] + c_.load_time;
            attr_[r] = c_.attr_min + (c_.attr_max - c_.attr_min) * u01();
        }
    }

    // Espera estimada na fila de uma atração: quantas partidas até esvaziá-la.
    double est_wait(std::size_t r) const {
        return static_cast<double>(qlen_[r]) / static_cast<double>(cap_[r]) * cyc_[r];
    }

    // Tempo de caminhada esperado (parte determinística): entra na decisão.
    double walk_base(std::uint32_t from, std::size_t to) const {
        const int d = std::abs(rx_[from] - rx_[to]) + std::abs(ry_[from] - ry_[to]);
        return 0.2 + 0.04 * d;
    }
    // Tempo de caminhada real: esperado + variação aleatória.
    double walk_time(std::uint32_t from, std::uint32_t to) {
        return walk_base(from, to) + expo(0.3);
    }

    // Cansaço com a recuperação natural (decaimento exponencial desde a última atualização).
    double fatigue_now(std::uint32_t v) const {
        const double dt = std::max(0.0, now_ - fat_t_[v]);
        return fat_[v] * std::exp(-dt / c_.fatigue_tau);
    }

    // Utilidade de uma atração. m amplifica o custo de esperar e de andar quando cansado.
    // A penalidade de repetição vale só para a última atração em que o visitante andou
    // (com penalidade infinita a utilidade é -inf e o peso logit é exatamente 0).
    double utility_of(std::uint32_t v, std::size_t r, double m) const {
        double u = attr_[r] - m * (c_.w_wait * est_wait(r) + c_.w_dist * walk_base(loc_[v], r));
        if (R_ > 1 && r == last_ride_[v]) u -= c_.repeat_penalty;
        return u;
    }

    // Escolha logit: K atrações sorteadas + a opção "descansar", com probabilidade
    // proporcional a exp(U / temperatura). Devolve o índice da atração ou R_ (= descansar).
    std::size_t choose_action(std::uint32_t v, double fatigue) {
        const double m = 1.0 + c_.fatigue_sens * fatigue;
        const std::size_t k = (c_.consider == 0 || c_.consider >= R_) ? R_ : c_.consider;
        if (k < R_) {  // sorteio parcial (Fisher-Yates): as k primeiras posições de perm_
            for (std::size_t i = 0; i < k; ++i)
                std::swap(perm_[i], perm_[i + static_cast<std::size_t>(rng_() % (R_ - i))]);
        }
        double umax = c_.rest_base + c_.rest_gain * fatigue;
        util_[k] = umax;
        for (std::size_t i = 0; i < k; ++i) {
            util_[i] = utility_of(v, perm_[i], m);
            umax = std::max(umax, util_[i]);
        }
        double total = 0.0;
        for (std::size_t i = 0; i <= k; ++i) {
            util_[i] = std::exp((util_[i] - umax) / c_.temperature);  // vira peso
            total += util_[i];
        }
        double u = u01() * total;
        std::size_t pick = k;
        for (std::size_t i = 0; i <= k; ++i) {
            if (u < util_[i]) { pick = i; break; }
            u -= util_[i];
        }
        return pick == k ? R_ : perm_[pick];
    }

    // ---- tratadores de evento ----
    void on_choose(std::uint32_t v) {
        const double f = fatigue_now(v);
        fatigue_sum_ += f;
        ++choices_;
        const std::size_t a = choose_action(v, f);
        if (a == R_) {  // decidiu descansar: continua Idle e decide de novo ao terminar
            ++rests_;
            const double len = expo(c_.rest_mean);
            fat_[v] = f * std::exp(-len / c_.rest_tau);  // descansar recupera rápido
            fat_t_[v] = now_ + len;
            schedule(v, EventType::Choose, now_ + len);
            return;
        }
        target_[v] = static_cast<std::uint32_t>(a);
        st_[v] = VisitorState::Walking;
        schedule(v, EventType::Arrive, now_ + walk_time(loc_[v], target_[v]));
    }

    void on_arrive(std::uint32_t v) {
        const std::uint32_t r = target_[v];
        loc_[v] = r;
        if (est_wait(r) > c_.balk_wait) {  // espera estimada longa demais: nem entra
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
        while (taken < cap_[r] && !dq.empty()) {
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
            schedule(en.v, EventType::EndRide, now_ + dur_[r]);
            ++taken;
        }
        riding_[r] = taken;
        schedule(id, EventType::Dispatch, now_ + cyc_[r]);
    }

    void on_end_ride(std::uint32_t v) {
        const std::uint32_t r = target_[v];
        if (riding_[r] > 0) --riding_[r];
        ++completed_;
        if (last_ride_[v] == r) ++repeats_;  // mesma atração do passeio anterior (só ocorre se permitido)
        last_ride_[v] = r;                   // lembra para a próxima decisão
        fat_[v] = fatigue_now(v) + c_.fatigue_per_ride;  // cada passeio cansa
        fat_t_[v] = now_;
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
