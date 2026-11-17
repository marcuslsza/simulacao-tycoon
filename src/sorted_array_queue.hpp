// sorted_array_queue.hpp
// ETAPA 2b - Lista ordenada em ARRAY (candidata forte em n pequeno).
//
// Ideia: manter os eventos num std::vector ordenado do MAIOR para o MENOR
// tempo. O próximo evento (o menor) fica no FIM do vetor, então pop_min é
// um pop_back: O(1). Já inserir exige achar a posição (busca binária,
// O(log n)) e deslocar os elementos à direita (O(n), mas é um memmove
// sequencial, muito amigável ao cache).
//
// Hipótese que o experimento testa: até certo n o memmove é tão barato que
// esta estrutura ganha do heap; depois o O(n) cobra a conta.
//
// Para cancelar/reagendar precisamos da chave (time, seq) do evento do
// visitante: guardamos em key_[id]. Com a chave, a busca binária acha o
// evento exato (a ordem é total, não há empates).
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "event.hpp"

namespace pq {

class SortedArrayQueue {
public:
    explicit SortedArrayQueue(std::size_t max_ids) : key_(max_ids) { a_.reserve(max_ids); }

    static const char* name() { return "array_ordenado"; }
    std::size_t size() const { return a_.size(); }

    void push(const Event& e) {
        a_.insert(insert_pos(e), e);
        key_[e.id] = {e.time, e.seq, true};
    }

    bool pop_min(Event& out) {
        if (a_.empty()) return false;
        out = a_.back();
        a_.pop_back();
        key_[out.id].present = false;
        return true;
    }

    bool cancel(std::uint32_t id) {
        if (!key_[id].present) return false;
        a_.erase(find(id));
        key_[id].present = false;
        return true;
    }

    bool reschedule(std::uint32_t id, double new_time, std::uint64_t new_seq) {
        if (!key_[id].present) return false;
        auto it = find(id);
        Event e = *it;
        a_.erase(it);
        e.time = new_time;
        e.seq = new_seq;
        push(e);
        return true;
    }

    bool check_invariant() const {
        std::size_t present = 0;
        for (const Key& k : key_) present += k.present ? 1 : 0;
        if (present != a_.size()) return false;
        for (std::size_t i = 0; i < a_.size(); ++i) {
            if (!key_[a_[i].id].present) return false;
            if (i > 0 && !before(a_[i], a_[i - 1])) return false;  // estritamente decrescente
        }
        return true;
    }

private:
    struct Key {
        double time = 0;
        std::uint64_t seq = 0;
        bool present = false;
    };

    std::vector<Event> a_;  // ordenado do maior para o menor; o mínimo é a_.back()
    std::vector<Key> key_;  // key_[id] = chave do evento pendente de 'id'

    // Primeira posição cujo elemento é menor que 'e' (todos antes dele são maiores).
    std::vector<Event>::iterator insert_pos(const Event& e) {
        return std::partition_point(a_.begin(), a_.end(),
                                    [&](const Event& x) { return before(e, x); });
    }

    std::vector<Event>::iterator find(std::uint32_t id) {
        Event probe{key_[id].time, key_[id].seq, id, EventType::Arrive};
        return insert_pos(probe);  // chave única => cai exatamente no evento
    }
};

}  // namespace pq
