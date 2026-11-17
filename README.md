# Fila de eventos de um simulador de parque: heap vs lista ordenada

Projeto em C++17 para comparar estruturas de fila de eventos (hold model de
Jones, 1986) com cancelamento/reagendamento, e depois usá-las num simulador de
parque. Etapas 1 a 6 prontas. Faltam a 7 (gráficos e análise) e a 8 (opcional).

## Plano e para que serve cada peça

| Etapa | Arquivo | Para que serve |
|---|---|---|
| 1 | `src/event.hpp` | Define o evento `(time, seq, id, type)` e a ordem total `(time, seq)`. É o "tijolo" comum: garante comparação justa e saída determinística. |
| 2a | `src/heap_queue.hpp` | Heap binário **indexado** (`pos_[id]`): push/pop/cancel/reschedule em O(log n). Baseline justo para cancelamento. |
| 2b | `src/sorted_array_queue.hpp` | Array ordenado (mínimo no fim): pop O(1), insert por busca binária + memmove. Candidato forte em n pequeno. |
| 2c | `src/sorted_list_queue.hpp` | Lista encadeada ordenada (clássico de Jones), indexada por id: cancel O(1), insert O(n) por varredura. |
| 3 | `tests/test_queues.cpp` | Compara cada estrutura com `std::set` em centenas de milhares de operações aleatórias (com muitos empates). Rode antes de medir. |
| 4 | `src/hold_bench.hpp` | Harness do hold model: carga pré-gerada, três distribuições de incremento, reagendamentos, checksum de corretude, mediana de repetições. |
| 5 | `src/main.cpp` | Linha de comando: roda a grade de experimentos, confere checksums, imprime tabela e CSV. |
| 6a | `src/park_sim.hpp` | Simulador de parque dirigido por eventos, em `template <class Q>`: o mesmo código roda com qualquer das três filas. |
| 6b | `src/park_main.cpp` | CLI do parque: modo benchmark (compara as filas e confere que geraram a mesma história) e modo demo (mapa ASCII). |
| 6c | `tests/test_park.cpp` | Testa o simulador: mesma história nas três filas, invariantes (um evento pendente por entidade) e cobertura de cancel/reschedule. |

## Como usar

```
make            # compila bench, park e os testes
make test       # testes de correção (faça primeiro)
./bench         # hold model, experimento rápido (padrão)

# grade completa para o relatório
./bench --sizes 10,20,50,100,200,500,1000,10000,100000 \
        --dists exp,uniform,bimodal --resched 0,0.1,0.3 \
        --reps 5 --csv resultados.csv
```

Dica: o ponto de cruzamento entre array e heap deve estar em n pequeno. Use
tamanhos intermediários (20, 50, 200, 500) para enxergá-lo bem.

### Simulador de parque

```
./park                                          # benchmark: 2000 visitantes, 3 filas
./park --visitors 10000 --minutes 120
./park --visitors 20000 --queue heap,array      # escolhe as estruturas
./park --visitors 30 --rides 4 --minutes 1200   # parque pequeno (n baixo)
./park --demo --visitors 1500 --minutes 120 --frame-every 10
./park --demo --delay-ms 400 --clear            # "animação" no terminal
```

Parâmetros úteis: `--rides` (1 a 26), `--capacity` (0 = automática),
`--incident-mean` (minutos entre quebras; menor = mais reagendamentos),
`--patience` (paciência média na fila), `--seed`, `--reps`.

## O simulador de parque

Visitantes alternam: pensar, caminhar até uma atração, esperar na fila, passear.
Cada atração parte a cada 2 min com até `capacity` visitantes. A fila tem
paciência (desistência) e atrações quebram de vez em quando.

Como cada operação da fila de eventos aparece no parque:

| Operação | Onde acontece |
|---|---|
| `push` | todo novo acontecimento agendado |
| `pop_min` | o laço principal (próximo evento no tempo) |
| `cancel` | visitante embarca: o "perder a paciência" dele deixa de existir |
| `reschedule` | quebra: quem está no passeio e a próxima partida ADIAM (chave aumenta); a fila perde paciência (chave diminui), em rajada |

Regra herdada da etapa 1: cada entidade (visitante, atração, gerador de
incidentes) tem exatamente UM evento pendente, e o estado dela diz o tipo dele.
Visitante: Idle=Choose, Walking=Arrive, Queued=GiveUp, Riding=EndRide.

Detalhes de implementação que valem uma frase no relatório:
- **Remoção preguiçosa na fila da atração**: quem desiste deixa uma entrada
  obsoleta; o `ticket` distingue a entrada atual de uma antiga do mesmo visitante.
- **Mesma história**: o simulador é determinístico, então heap, array e lista
  geram exatamente a mesma simulação. O benchmark confere isso; só o tempo difere.
- **Capacidade automática** (~metade da demanda) para as filas saturarem e a
  paciência, a desistência e o cancelamento acontecerem de verdade.

## Decisões de projeto (para justificar no relatório)

- **Um evento pendente por visitante**: `id` é o handle. Permite cancelar sem
  varredura em estruturas indexadas.
- **Estruturas indexadas nas três**: o baseline não pode ser prejudicado por
  falta de índice. Se a lista perder, perde de forma justa.
- **Templates em vez de classe virtual**: sem custo de despacho virtual na medição.
- **Carga pré-gerada**: o gerador aleatório fora do cronômetro e a mesma
  sequência de operações para todas as estruturas.
- **Checksum**: se as estruturas divergirem, o benchmark aborta.
- **Mediana**: robusta a picos de ruído do sistema.

## Limitações conhecidas

- O parque não tem pathfinding: o tempo de caminhada é a distância de Manhattan
  no mapa. Para n baixo, o custo do próprio simulador (sorteios, tratadores)
  domina o tempo por evento, e as filas ficam parecidas. É um achado, não um bug.
- Com a configuração padrão a regra de recusa de fila (`balk_wait`) quase nunca
  dispara. Use menos capacidade (`--capacity`) ou mais visitantes se quiser vê-la.

- `ns/hold` inclui o custo do laço (leitura da carga, checksum). É o mesmo para
  as três, mas não é zero; para n muito pequeno vale subtrair um laço vazio.
- A lista é pulada acima de 30.000 por padrão (`--skip-list-above`).
- O array a 100.000 é lento de propósito (é o ponto do experimento).
- Meça com `-O2`, CPU em desempenho fixo e poucos programas abertos.

## Próximas etapas

7. Gráficos (tempo × n em escala log-log) a partir dos CSVs e relatório.
   Sugestão: rodar `./bench` (hold) e `./park` (parque) com os mesmos n e
   comparar se o ponto de cruzamento muda.
8. Opcional: timing wheel ou calendar queue como quarta estrutura.
