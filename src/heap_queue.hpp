// heap_queue.hpp
// Heap binário INDEXADO (a estrutura "sofisticada" do estudo).
//
// Heap comum: push e pop_min em O(log n), mas cancelar um evento do meio
// exige varrer o vetor (O(n)). Para um simulador com desistências e
// reagendamentos isso seria um baseline injusto. Solução: guardar em pos_[id] onde o evento
// de cada visitante está no vetor. Assim cancel e reschedule viram O(log n).
//
// CONTRATO (igual nas três estruturas; o benchmark usa só isso):
//   explicit Q(size_t max_ids)
//   void   push(const Event&)                     // id não pode ter evento pendente
//   bool   pop_min(Event& out)                    // false se vazia
//   bool   cancel(uint32_t id)                    // true se existia
//   bool   reschedule(uint32_t id, double t, uint64_t seq)
//   size_t size() const
//   static const char* name()
//   bool   check_invariant() const                // só para os testes
#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include "event.hpp"

namespace pq {

class IndexedBinaryHeap {
public:
    explicit IndexedBinaryHeap(std::size_t max_ids) : pos_(max_ids, kAbsent) {
        heap_.reserve(max_ids);
    }

    static const char* name() { return "heap_indexado"; }
    std::size_t size() const { return heap_.size(); }

    void push(const Event& e) {
        heap_.push_back(e);
        std::size_t i = heap_.size() - 1;
        pos_[e.id] = static_cast<std::uint32_t>(i);
        sift_up(i);
    }

    bool pop_min(Event& out) {
        if (heap_.empty()) return false;
        out = heap_[0];
        remove_at(0);
        return true;
    }

    bool cancel(std::uint32_t id) {
        if (pos_[id] == kAbsent) return false;
        remove_at(pos_[id]);
        return true;
    }

    // Muda a chave do evento do visitante 'id' no lugar. A chave pode diminuir
    // (buff/atração mais rápida) OU aumentar (atraso): por isso sobe e desce.
    bool reschedule(std::uint32_t id, double new_time, std::uint64_t new_seq) {
        if (pos_[id] == kAbsent) return false;
        std::size_t i = pos_[id];
        heap_[i].time = new_time;
        heap_[i].seq = new_seq;
        i = sift_up(i);
        sift_down(i);
        return true;
    }

    bool check_invariant() const {
        for (std::size_t i = 0; i < heap_.size(); ++i) {
            if (pos_[heap_[i].id] != i) return false;
            if (i > 0 && before(heap_[i], heap_[(i - 1) / 2])) return false;
        }
        return true;
    }

private:
    static constexpr std::uint32_t kAbsent = std::numeric_limits<std::uint32_t>::max();

    std::vector<Event> heap_;          // árvore binária completa guardada em array
    std::vector<std::uint32_t> pos_;   // pos_[id] = índice do evento de 'id' em heap_

    // Remove o elemento do índice i: traz o último para o buraco e conserta.
    void remove_at(std::size_t i) {
        pos_[heap_[i].id] = kAbsent;
        Event last = heap_.back();
        heap_.pop_back();
        if (i == heap_.size()) return;  // removemos justamente o último
        heap_[i] = last;
        pos_[last.id] = static_cast<std::uint32_t>(i);
        i = sift_up(i);
        sift_down(i);
    }

    // "Técnica do buraco": segura o elemento e desloca pais/filhos em vez de
    // trocar par a par (menos escritas). Devolve a posição final.
    std::size_t sift_up(std::size_t i) {
        Event e = heap_[i];
        while (i > 0) {
            std::size_t p = (i - 1) / 2;
            if (!before(e, heap_[p])) break;
            heap_[i] = heap_[p];
            pos_[heap_[i].id] = static_cast<std::uint32_t>(i);
            i = p;
        }
        heap_[i] = e;
        pos_[e.id] = static_cast<std::uint32_t>(i);
        return i;
    }

    void sift_down(std::size_t i) {
        Event e = heap_[i];
        const std::size_t n = heap_.size();
        for (;;) {
            std::size_t c = 2 * i + 1;
            if (c >= n) break;
            if (c + 1 < n && before(heap_[c + 1], heap_[c])) ++c;  // menor dos filhos
            if (!before(heap_[c], e)) break;
            heap_[i] = heap_[c];
            pos_[heap_[i].id] = static_cast<std::uint32_t>(i);
            i = c;
        }
        heap_[i] = e;
        pos_[e.id] = static_cast<std::uint32_t>(i);
    }
};

}  // namespace pq
