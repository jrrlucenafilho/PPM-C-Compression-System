# PPM-C compressor (ITI 2026.2)

Implementacao do algoritmo **PPM-C com exclusao** para alfabetos de 1, 4 e 8 bits,
com monitoramento de taxa local e estrategias de reset e poda, para o trabalho de
Introducao a Teoria da Informacao.

## Compilar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/ppm selftest
```

`selftest` verifica ida e volta (compressao + descompressao) para os tres
alfabetos, ordens de 0 ate o maximo, dados aleatorios, arquivos vazios e os tres
modos (sem adaptacao, reset, poda).

## Uso

```bash
# comprimir
./build/ppm c entrada saida.ppm --bits 8 --kmax 6 --mode none

# descomprimir
./build/ppm d saida.ppm original

# integridade
cmp entrada original && echo OK

# reset com janela de 50.000 simbolos e tolerancia de 20%
./build/ppm c entrada saida.ppm --bits 8 --kmax 6 --mode reset --j 50000 --p 20

# curva de comprimento medio progressivo (3.2 / 3.3)
./build/ppm c entrada saida.ppm --bits 8 --kmax 6 --trace curva.csv --interval 10000
```

O cabecalho do arquivo `.ppm` guarda alfabeto, `Kmax`, modo, `j`, `p`, tamanho
original e o limite de memoria do modelo. O descompressor reconstroi exatamente o
mesmo modelo, inclusive resets e podas.

## Decisoes de implementacao

- **Escape metodo C (Moffat):** em um contexto com `n` ocorrencias e `d` simbolos
  distintos, a frequencia de escape e `d`, logo `P(escape) = d/(n+d)`.
- **Exclusao:** ao escapar de um contexto, todos os simbolos desse contexto sao
  removidos das ordens inferiores, inclusive da distribuicao uniforme de ordem -1.
  Simbolos excluidos saem tambem de `n` e `d` (as probabilidades sao renormali-
  zadas sobre o que restou). Se todos os simbolos do contexto estao excluidos,
  nenhum codigo e emitido (o escape tem probabilidade 1).
- **Ordem -1:** uniforme sobre o alfabeto menos os simbolos excluidos.
- **Atualizacao:** todos os contextos de `0` a `Kmax` sao atualizados depois de
  cada simbolo (sem update exclusion).
- **Reescala:** quando `total + distintos > 8192`, as contagens sao reduzidas por
  `(c+1)/2`. Compressor e descompressor fazem isso no mesmo ponto.
- **Monitor:** o custo de cada simbolo e `-log2(freq/total)`, acumulado em ponto
  fixo Q16 (tabela de log2). A cada `j` simbolos compara-se a janela atual com a
  anterior; se a atual for mais de `p%` pior, dispara reset ou poda. O custo ideal
  e usado (nao a posicao no arquivo) para que compressor e descompressor cheguem
  a mesma decisao.
- **Reset:** limpa todas as tabelas e o historico de contexto.
- **Poda:** remove contextos vistos uma unica vez e reduz as demais contagens por
  `(c+1)/2`. E o criterio de "eliminacao de contextos por frequencia".
- **Memoria:** no maximo `--mem` contextos (padrao 8 milhoes). Ao atingir o
  limite, contextos novos deixam de ser criados; os existentes continuam sendo
  atualizados. O limite vai no cabecalho para manter a sincronia.
- **Alfabetos:** A1 usa cada bit como simbolo (MSB primeiro); A4 usa nibbles;
  A8 usa bytes. O ultimo simbolo e completado com zeros e o tamanho original em
  bytes e gravado no cabecalho. `bits/byte` e a metrica comum aos tres alfabetos.

## Experimentos (3.1, 3.2, 3.3)

```bash
# baixa Silesia e monta data/calib.bin (1 MB de cada arquivo) e data/all.bin
./scripts/get_data.sh

# grade 3.1: Kmax x j x p x modo, no fluxo de calibracao
THREADS=12 ./scripts/run31.sh
QUICK=1 THREADS=8 ./scripts/run31.sh     # grade reduzida, para testar
RESUME=1 THREADS=12 ./scripts/run31.sh   # continua uma grade interrompida

# melhores configuracoes no corpus completo + curvas + comparacao com 7-Zip
# (cada passada grava o CSV final e a curva 3.3 ao mesmo tempo)
B1=17 J1=50000 P1=30 B4=8 J4=10000 P4=50 B8=5 J8=10000 P8=50 \
  ./scripts/run_final.sh

# tempos de compressao/descompressao das 9 melhores configuracoes da calibracao
./scripts/run_dec_times.sh

# figuras e tabela resumo
python3 scripts/plots.py
```

Saidas em `results/`: `31_A{1,4,8}.csv` (grade), `summary_31.csv`,
`31_dec_times.csv`, `final_A*_*.csv`, `commercial.csv`, `trace_*.csv` e
`figs/*.png`.

### Por que calibrar em `calib.bin`

A grade completa no corpus inteiro (211 MB) custaria centenas de horas de CPU: o
A1 com `Kmax=32` chega a ~1 Msimbolo/s e o corpus tem 1,7 bilhao de bits. O fluxo
de calibracao usa 1 MB de cada um dos 12 arquivos (12 MB, 11 transicoes), roda a
grade completa ali e depois valida as melhores configuracoes no corpus inteiro.
Isso preserva o objetivo do item 3.1 (efeito de `Kmax`, `j`, `p` e dos modos) sem
inviabilizar o prazo.

## Limitacoes conhecidas

- A ordem alta em A1 (`Kmax` > ~16) cria contextos quase unicos. Alem de nao
  comprimir melhor, encosta no limite de memoria. O grafico de `bits/byte` vs
  `Kmax` mostra essa saturacao — e um resultado, nao um defeito.
- O `mem_cap` e global e preenchido das ordens baixas para as altas; em `Kmax`
  muito alto as ordens mais altas podem nao caber. Aumente `--mem` para testar.
- Tempo de compactacao do A8 e ~1,5 Msimbolo/s (single thread) nesta
  implementacao; a grade usa paralelismo por combinacao de parametros.
