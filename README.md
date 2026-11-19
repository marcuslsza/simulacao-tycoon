# Fila de eventos de um simulador de parque: heap indexado vs array e lista ordenados

Código do artigo *Heap indexado, array ordenado e lista ordenada como filas de eventos
com cancelamento e reagendamento: do modelo hold a um simulador de parque* (C++17, sem
dependências). Três filas com a mesma interface (`push`, `pop_min`, `cancel(id)`,
`reschedule(id, t, seq)`), todas indexadas pelo identificador do evento, são medidas no
modelo hold de Jones (1986), estendido com reagendamentos, e dentro de um simulador de
parque dirigido por eventos.

## Arquivos

| Arquivo | Para que serve |
|---|---|
| `src/event.hpp` | Evento `(time, seq, id, type)` de 24 bytes e ordem total `(time, seq)`: comparação justa e saída determinística. |
| `src/heap_queue.hpp` | Heap binário **indexado** (`pos_[id]`): todas as operações em O(log n). |
| `src/sorted_array_queue.hpp` | Array ordenado (mínimo no fim): pop O(1); push/cancel/reschedule por busca binária + `memmove`. |
| `src/sorted_list_queue.hpp` | Lista duplamente encadeada ordenada, nós indexados por id: cancel O(1), inserção O(n) por varredura. |
| `src/hold_bench.hpp` | Harness do hold: carga pré-gerada, três distribuições de incremento, reagendamentos, checksum, mediana. |
| `src/main.cpp` | `./bench`: roda a grade do hold, confere checksums, imprime tabela e CSV. |
| `src/park_sim.hpp` | Simulador de parque em `template <class Q>`: o mesmo código roda com qualquer das três filas. |
| `src/park_main.cpp` | `./park`: simula com as três filas e confere que geraram exatamente a mesma história. |
| `src/cli.hpp` | Leitura das opções de linha de comando (usada por `bench` e `park`). |
| `tests/test_queues.cpp` | Compara cada fila com `std::set` em centenas de milhares de operações aleatórias, com muitos empates de tempo. |
| `tests/test_park.cpp` | Parque: mesma história nas três filas, invariantes (um evento pendente por entidade), cobertura de cancel/reschedule, parâmetros das atrações, modelo de decisão e regra de variedade. |

## Como usar

```
make            # compila bench, park e os testes
make test       # testes de correção (faça primeiro)
./bench         # hold, experimento rápido (padrão)
./park          # parque: 2000 visitantes, três filas
```

**Hold, grade do artigo** (n de 10 a 10^5, 3 distribuições, 0/10/30% de reagendamentos,
10^5 holds por execução, mediana de 5 repetições):

```
./bench --sizes 10,20,50,100,200,500,1000,10000,100000 \
        --dists exp,uniform,bimodal --resched 0,0.1,0.3 \
        --reps 5 --csv resultados.csv
```

**Parque.** O artigo varia `--visitors` (30 a 30000), `--rides` (4 a 12), `--seed` (5
sementes; 3 a partir de 10000 visitantes) e `--reps` (3; 1 a partir de 10000) e usa a
mediana entre sementes do `ns/evento`. O `n` do artigo é o `pico pendente` impresso.

```
./park --visitors 10000 --rides 12 --seed 1 --reps 1
./park --visitors 30 --rides 4 --minutes 1200       # parque pequeno (n baixo)
./park --visitors 20000 --queue heap,array          # escolhe as estruturas
./park --repeat-penalty 15                          # repetir a atração só "custa" 15 min de fila
```

Opções do parque: `--visitors`, `--rides` (1 a 26), `--minutes`, `--seed`, `--reps`,
`--queue`, `--skip-list-above`, `--capacity N`, `--capacity-range MIN,MAX`,
`--ride-range MIN,MAX`, `--load-time`, `--balk-wait`, `--patience`, `--incident-mean`
e as da decisão do visitante: `--attr-range`, `--w-wait`, `--w-dist`, `--temperature`,
`--consider`, `--fatigue-per-ride`, `--fatigue-tau`, `--fatigue-sens`, `--rest-base`,
`--rest-gain`, `--rest-mean`, `--rest-tau`, `--repeat-penalty`. Os padrões estão em
`ParkConfig` (`src/park_sim.hpp`); todos os pesos são suposições do modelo.

## O simulador de parque

Visitantes alternam: pensar, caminhar até uma atração, esperar na fila, passear (ou
descansar). Cada atração tem duração (2 a 10 min) e capacidade próprias, sorteadas uma
vez pela semente e fixas; ela parte a cada (duração + embarque) min, levando até a sua
capacidade. O visitante **recusa** a fila se a espera estimada (fila / capacidade x
intervalo entre partidas) passa de 30 min e **desiste** se perde a paciência (média de
25 min). Atrações quebram de vez em quando.

A cada decisão, o visitante avalia K atrações sorteadas (`--consider`, padrão 6; 0 =
todas) mais a opção "descansar" e escolhe com probabilidade proporcional a
`exp(U / temperatura)` (logit). As utilidades são em "minutos equivalentes de espera":

```
U(atração)   = atratividade - m * ( w_wait * espera_estimada + w_dist * caminhada )
                            - repeat_penalty   (só na última atração em que andou)
U(descansar) = rest_base + rest_gain * cansaço
m            = 1 + fatigue_sens * cansaço
```

O cansaço sobe a cada passeio e decai com o tempo; com `repeat_penalty` infinito (padrão)
ninguém anda duas vezes seguidas na mesma atração (recusar a fila ou desistir não conta
como passeio), com 0 a regra é desligada e com um valor finito o visitante só prefere variar.

Como cada operação da fila aparece no parque:

| Operação | Onde acontece |
|---|---|
| `push` | todo novo acontecimento agendado |
| `pop_min` | o laço principal (próximo evento no tempo) |
| `cancel` | visitante embarca: o "perder a paciência" dele deixa de existir |
| `reschedule` | quebra: quem está no passeio e a próxima partida ADIAM (chave aumenta); a fila perde paciência (chave diminui), em rajada |

Cada entidade (visitante, atração, gerador de quebras) tem exatamente UM evento pendente,
e o estado dela diz o tipo dele (Idle=Choose, Walking=Arrive, Queued=GiveUp,
Riding=EndRide). Detalhes de implementação:

- **Remoção preguiçosa na fila da atração**: quem desiste deixa uma entrada obsoleta; um
  `ticket` distingue a entrada atual de uma antiga do mesmo visitante.
- **Mesma história**: o simulador é determinístico, então as três filas geram exatamente a
  mesma simulação; o programa confere isso e só o tempo difere.
- **Parâmetros das atrações vêm da semente**: as três filas veem o mesmo parque, mas recusas
  e esperas variam bastante entre sementes (muda quais atrações são gargalo). Rode várias.
- **Capacidade automática** (~metade da demanda) para que as filas saturem e desistência,
  recusa e cancelamento aconteçam de verdade.
- **A decisão custa O(K) por escolha**, igual nas três filas: dilui a parcela da fila no
  `ns/evento` total.

## Decisões de projeto

- **Um evento pendente por entidade**: `id` é o handle, e cancelar/reagendar não varre nada.
- **Estruturas indexadas nas três**: o baseline não é prejudicado por falta de índice; se a
  lista perder, perde de forma justa.
- **Templates em vez de classe virtual**: sem despacho virtual na medição.
- **Carga pré-gerada** fora do cronômetro e idêntica para todas as estruturas.
- **Checksum** (FNV-1a) dos ids extraídos: se as estruturas divergirem, o benchmark aborta.
- **Mediana** das repetições: robusta a picos de ruído do sistema.

## Limitações conhecidas

- O parque não tem pathfinding: a caminhada é a distância de Manhattan no mapa. Para n
  baixo o custo do simulador (sorteios, tratadores) domina e as filas ficam parecidas.
- O `ns/evento` do parque inclui a lógica da simulação; para isolar a fila, compare com o hold.
- `ns/hold` inclui o custo do laço (leitura da carga, checksum), igual nas três.
- O `pico pendente` do parque vale `visitantes + atrações + 2`: o contador soma 1 ao tamanho
  da fila depois do tratador, que já reinseriu o evento; o pico exato seria `visitantes + atrações + 1`.
- A lista é pulada acima de 30000 por padrão (`--skip-list-above`); o array em 10^5 é lento
  de propósito (é o ponto do experimento).
- Meça com `-O2`, CPU em desempenho fixo e poucos programas abertos.
