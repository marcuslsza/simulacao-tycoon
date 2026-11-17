// sorted_list_queue.hpp
// ETAPA 2c - Lista ordenada ENCADEADA (o baseline clássico de Jones, 1986).
//
// Lista duplamente encadeada em ordem crescente: o mínimo está na cabeça, então
// pop_min é O(1). Inserir exige VARRER a lista a partir da cabeça até achar a
// posição: O(n), e cada passo é um salto de memória (pouco amigável ao cache).
//
// Para a comparação ser justa com o heap indexado, a lista também é "indexada":
// os nós vivem em vetores indexados pelo id do visitante (como cada visitante
// tem no máximo um evento, um nó por id basta). Assim cancel é O(1), sem
// varredura. O que sobra de caro é só a inserção ordenada.
//
// Obs.: uma lista com new/delete por nó seria ainda mais lenta (alocação +
// cache). Este é o melhor caso razoável para a lista, o que fortalece a
// conclusão se ela mesmo assim perder.
#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include "event.hpp"

namespace pq {

class SortedLinkedListQueue {
public:
    explicit SortedLinkedListQueue(std::size_t max_ids)
        : ev_(max_ids), prev_(max_ids, kNil), next_(max_ids, kNil), present_(max_ids, 0) {}

    static const char* name() { return "lista_ordenada"; }
    std::size_t size() const { return size_; }

    void push(const Event& e) {
        ev_[e.id] = e;
        link_sorted(e.id);
    }

    bool pop_min(Event& out) {
        if (head_ == kNil) return false;
        out = ev_[head_];
        unlink(head_);
        return true;
    }

    bool cancel(std::uint32_t id) {
        if (!present_[id]) return false;
        unlink(id);
        return true;
    }

    bool reschedule(std::uint32_t id, double new_time, std::uint64_t new_seq) {
        if (!present_[id]) return false;
        unlink(id);
        ev_[id].time = new_time;
        ev_[id].seq = new_seq;
        link_sorted(id);
        return true;
    }

    bool check_invariant() const {
        std::size_t count = 0;
        std::uint32_t p = kNil;
        for (std::uint32_t cur = head_; cur != kNil; cur = next_[cur]) {
            if (!present_[cur] || prev_[cur] != p) return false;
            if (p != kNil && !before(ev_[p], ev_[cur])) return false;  // estritamente crescente
            p = cur;
            ++count;
        }
        return p == tail_ && count == size_;
    }

private:
    static constexpr std::uint32_t kNil = std::numeric_limits<std::uint32_t>::max();

    std::vector<Event> ev_;             // ev_[id] = evento do visitante id
    std::vector<std::uint32_t> prev_;   // vizinhos na lista, indexados por id
    std::vector<std::uint32_t> next_;
    std::vector<std::uint8_t> present_;
    std::uint32_t head_ = kNil;
    std::uint32_t tail_ = kNil;
    std::size_t size_ = 0;

    // Varre da cabeça enquanto os nós forem menores que o novo, e insere antes
    // do primeiro maior (ou no fim).
    void link_sorted(std::uint32_t id) {
        const Event& e = ev_[id];
        std::uint32_t cur = head_;
        while (cur != kNil && before(ev_[cur], e)) cur = next_[cur];
        std::uint32_t before_node = (cur == kNil) ? tail_ : prev_[cur];
        prev_[id] = before_node;
        next_[id] = cur;
        if (before_node == kNil) head_ = id; else next_[before_node] = id;
        if (cur == kNil) tail_ = id; else prev_[cur] = id;
        present_[id] = 1;
        ++size_;
    }

    void unlink(std::uint32_t id) {
        std::uint32_t p = prev_[id], n = next_[id];
        if (p == kNil) head_ = n; else next_[p] = n;
        if (n == kNil) tail_ = p; else prev_[n] = p;
        present_[id] = 0;
        --size_;
    }
};

}  // namespace pq
