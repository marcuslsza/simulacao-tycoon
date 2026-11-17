// event.hpp
// ETAPA 1 - O "tijolo" comum: o que é um evento e como eles são ordenados.
//
// Todas as estruturas (heap, array ordenado, lista ordenada) guardam o MESMO
// tipo de evento e usam a MESMA regra de ordenação. Isso é o que torna a
// comparação justa: a única coisa que muda entre os experimentos é a estrutura.
#pragma once

#include <cstddef>
#include <cstdint>

namespace pq {

// Rótulos de evento do simulador de parque (etapa 6). No benchmark do hold
// model eles não importam; no simulador cada rótulo é um tipo de acontecimento:
//   Choose   - o visitante decide a próxima atração
//   Arrive   - o visitante termina de caminhar e chega à fila
//   GiveUp   - o visitante perde a paciência e sai da fila
//   Dispatch - a atração parte com os visitantes que estão na frente da fila
//   EndRide  - o visitante desce da atração
//   Incident - uma atração quebra (atrasa quem está dentro e irrita a fila)
enum class EventType : std::uint8_t { Choose, Arrive, GiveUp, Dispatch, EndRide, Incident };

struct Event {
    double        time;  // instante ABSOLUTO em que o evento ocorre
    std::uint64_t seq;   // desempate: mesmo 'time' => sai primeiro quem foi criado antes (FIFO)
    std::uint32_t id;    // visitante dono do evento
    EventType     type;
};

// REGRA-CHAVE DO PROJETO: cada visitante tem no máximo UM evento pendente.
// Por isso 'id' funciona como "handle": para cancelar ou reagendar basta dizer
// o id do visitante, e cada estrutura decide como achar o evento dele
// (heap: vetor de posições; array: busca binária; lista: ponteiro por id).

// Ordem total (time, seq). Como 'seq' é único, nunca existem dois eventos
// "iguais": a saída é determinística e idêntica em todas as estruturas, o que
// permite verificar correção comparando as sequências (checksum).
inline bool before(const Event& a, const Event& b) {
    return a.time < b.time || (a.time == b.time && a.seq < b.seq);
}

}  // namespace pq
