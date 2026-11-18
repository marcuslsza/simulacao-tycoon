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
| 6c | `tests/test_park.cpp` | Testa o simulador: mesma história nas três filas, invariantes (um evento pendente por entidade), cobertura de cancel/reschedule e a regra de variedade (sem repetir a atração em sequência). |

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
./park --capacity-range 20,60 --ride-range 2,10 --balk-wait 30
./park --repeat-penalty 15                      # repetir a atração vira só "caro", não proibido
./park --repeat-penalty 0                       # sem variedade (comportamento da versão anterior)
```

Parâmetros úteis:
- `--rides` (1 a 26), `--seed`, `--reps`.
- `--capacity-range MIN,MAX`: capacidade máxima de embarque, sorteada UMA vez por
  atração e fixa depois. Sem a opção, é automática (0,5x a 1,5x de uma
  capacidade-base calculada pelo número de visitantes). `--capacity N` dá o
  mesmo valor N para todas.
- `--ride-range MIN,MAX`: duração do passeio em minutos, sorteada uma vez por
  atração (padrão 2,10). `--load-time` é o embarque entre partidas (padrão 1).
- `--balk-wait`: acima dessa espera estimada (min) o visitante nem entra na fila
  (padrão 30). `--patience`: paciência média de quem já está na fila (padrão 25).
- `--incident-mean`: minutos entre quebras (menor = mais reagendamentos).
- Decisão do visitante (ver a seção abaixo): `--attr-range`, `--w-wait`,
  `--w-dist`, `--temperature`, `--consider`, `--fatigue-per-ride`,
  `--fatigue-tau`, `--fatigue-sens`, `--rest-base`, `--rest-gain`,
  `--rest-mean`, `--rest-tau`, `--repeat-penalty`.

## O simulador de parque

Visitantes alternam: pensar, caminhar até uma atração, esperar na fila, passear.
Cada atração tem duração (2 a 10 min) e capacidade máxima próprias, sorteadas
uma vez e fixas. Ela parte a cada (duração + embarque) min, levando até a sua
capacidade. Há dois jeitos de não esperar na fila: **recusar** (a espera
estimada, tamanho da fila / capacidade x intervalo entre partidas, passa de
30 min e o visitante nem entra) e **desistir** (já na fila, perde a paciência).
Atrações quebram de vez em quando.

### Como o visitante decide

A cada decisão, o visitante avalia K atrações sorteadas (`--consider`, padrão 6;
0 = todas) mais a opção "descansar", e escolhe com probabilidade proporcional a
`exp(U / temperatura)` (escolha logit). As utilidades são em "minutos
equivalentes de espera":

```
U(atração)   = atratividade - m * ( w_wait * espera_estimada + w_dist * caminhada )
                            - repeat_penalty   (só se for a última atração em que andou)
U(descansar) = rest_base + rest_gain * cansaço
m            = 1 + fatigue_sens * cansaço
```

| Variável | Efeito | Parâmetros |
|---|---|---|
| **Atratividade** (fixa por atração, sorteada uma vez) | quanto maior, mais escolhida | `--attr-range` |
| **Espera estimada** (fila / capacidade x intervalo entre partidas) | quanto maior, menos atraente | `--w-wait` |
| **Distância** (tempo de caminhada esperado até a atração) | quanto mais longe, menos atraente | `--w-dist` |
| **Cansaço** (+0,25 por passeio, decai com o tempo) | torna o descanso mais atraente e o visitante menos tolerante a fila e caminhada (m maior) | `--fatigue-per-ride`, `--fatigue-tau`, `--fatigue-sens`, `--rest-gain` |
| **Variedade** (última atração em que andou) | evita andar duas vezes seguidas na mesma; infinito (padrão) = nunca, valor finito = só prefere variar, 0 = desliga | `--repeat-penalty` |
| **Descanso** | dura em média 8 min e derruba o cansaço rápido | `--rest-base`, `--rest-mean`, `--rest-tau` |
| Aleatoriedade | menor temperatura = mais determinista | `--temperature` |

Fora da escolha, o visitante ainda tem a **recusa** (espera estimada > `--balk-wait`)
e a **paciência** na fila (`--patience`), que só afetam quem já decidiu ir.
Não entram na decisão: atração quebrada, duração do passeio e capacidade (esta
só indiretamente, pela espera estimada).

Variedade: a penalidade vale só para a atração do **último passeio concluído** e não é
multiplicada pelo cansaço. Recusar a fila ou desistir não conta como passeio (o visitante
não andou, então pode tentar a mesma atração de novo). Descansar também não apaga a
memória: andou na A, descansou, a próxima escolha ainda penaliza a A. Com uma única
atração a regra não se aplica (não há alternativa). Com penalidade infinita o peso logit
da atração repetida é exatamente 0, então a proibição não depende da `--temperature`.
No modelo antigo ~12 a 13% dos passeios eram repetições imediatas; o `./park` imprime
essa contagem (`mesma atracao em sequencia`), então dá para citar o antes/depois no
relatório (`--repeat-penalty 0` x padrão). Em média, quem não pode repetir descansa um
pouco mais (cerca de 6% mais descansos nos testes com 3000 visitantes).
**As histórias mudam em relação às versões anteriores**: refaça os CSVs/medições do
parque. Com `--repeat-penalty 0` a simulação é idêntica à da versão anterior (mesma
semente, mesmo checksum).

Calibração: `--rest-base -10` (padrão) faz o descanso vir de passeios
consecutivos. Com `0`, visitantes descansam até pouco cansados, porque filas
longas já tornam o descanso competitivo. Todos os pesos são suposições do modelo;
documente-os no relatório e, se possível, varie-os.

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
- **A atração proibida ainda ocupa uma vaga na amostra** de K atrações avaliadas
  (`--consider`): o sorteio é feito antes e a penalidade entra na utilidade. É a
  mesma ideia de "o visitante lembrou dela, mas descartou".
- **Capacidade automática** (~metade da demanda) para as filas saturarem e a
  paciência, a desistência, a recusa e o cancelamento acontecerem de verdade.
- **Parâmetros das atrações vêm da semente**: as três filas veem exatamente o
  mesmo parque. Mas o resultado do parque (recusas, esperas) varia bastante
  entre sementes, porque o sorteio de capacidade e duração muda quais atrações
  são gargalo. Para o relatório, rode várias sementes (`--seed`).
- **A decisão custa O(K) por escolha** (K = `--consider`). Esse custo é igual para
  as três filas, mas dilui a parcela de tempo da fila no `ns/evento` total.

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
- O simulador mede o tempo por evento do conjunto (fila + lógica do parque). Para
  isolar o custo da fila, compare com o benchmark do hold model.

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
