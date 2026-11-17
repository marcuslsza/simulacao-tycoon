#!/usr/bin/env python3
"""
Lê um arquivo CSV e gera gráficos de colunas (barras verticais) em arquivos PNG.

Para cada coluna numérica do CSV é gerado um gráfico, usando como categorias
(eixo X) a primeira coluna de texto — ou o número da linha, se não houver.

Dependências:
    pip install pandas matplotlib

Exemplos de uso:
    python csv_para_graficos.py vendas.csv
    python csv_para_graficos.py vendas.csv --x Mes --y Receita Lucro
    python csv_para_graficos.py dados.csv --sep ";" --decimal "," --saida graficos
    python csv_para_graficos.py pedidos.csv --x Categoria --agg sum
"""

import argparse
import re
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")  # gera imagens sem precisar de janela/interface gráfica
import matplotlib.pyplot as plt
import pandas as pd


def ler_csv(caminho, sep, decimal, encoding):
    """Lê o CSV detectando o separador e tentando codificações comuns."""
    codificacoes = [encoding] if encoding else ["utf-8-sig", "latin-1"]
    ultimo_erro = None
    for enc in codificacoes:
        try:
            return pd.read_csv(
                caminho,
                sep=sep,  # None = detecta automaticamente (, ; tab etc.)
                engine="python",
                decimal=decimal,
                encoding=enc,
            )
        except UnicodeDecodeError as erro:
            ultimo_erro = erro
    raise ultimo_erro


def nome_seguro(texto):
    """Transforma um texto em um nome de arquivo válido."""
    limpo = re.sub(r"[^\w\-]+", "_", str(texto), flags=re.UNICODE).strip("_")
    return limpo or "coluna"


def formatar_valor(valor):
    """Formata o rótulo exibido em cima de cada barra (padrão brasileiro)."""
    if float(valor).is_integer():
        texto = f"{valor:,.0f}"
    else:
        texto = f"{valor:,.2f}"
    # 12,500.50 -> 12.500,50
    return texto.replace(",", "X").replace(".", ",").replace("X", ".")


def gerar_grafico(serie, nome_x, nome_y, destino, dpi):
    """Cria um gráfico de colunas a partir de uma série e salva em PNG."""
    fig, ax = plt.subplots(figsize=(10, 6))

    categorias = serie.index.astype(str)
    barras = ax.bar(categorias, serie.values, color="#4C78A8")

    ax.set_title(f"{nome_y} por {nome_x}", fontsize=14, fontweight="bold")
    ax.set_xlabel(nome_x)
    ax.set_ylabel(nome_y)
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)

    # Gira os rótulos do eixo X quando há muitas categorias ou nomes longos
    if len(categorias) > 6 or max(len(c) for c in categorias) > 8:
        plt.setp(ax.get_xticklabels(), rotation=45, ha="right")

    # Mostra o valor em cima das barras (se não forem barras demais)
    if len(serie) <= 20:
        ax.bar_label(
            barras,
            labels=[formatar_valor(v) for v in serie.values],
            padding=3,
            fontsize=9,
        )

    fig.tight_layout()
    fig.savefig(destino, dpi=dpi)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(
        description="Gera gráficos de colunas (PNG) a partir de um arquivo CSV."
    )
    parser.add_argument("arquivo", help="caminho do arquivo CSV")
    parser.add_argument("--x", help="coluna usada como categorias (eixo X)")
    parser.add_argument(
        "--y", nargs="+", help="colunas numéricas a plotar (padrão: todas)"
    )
    parser.add_argument(
        "--saida", default="graficos", help="pasta de saída (padrão: graficos)"
    )
    parser.add_argument(
        "--sep", default=None, help="separador do CSV (padrão: detecta sozinho)"
    )
    parser.add_argument(
        "--decimal",
        default=".",
        help='separador decimal; use "," para números como 1234,56 (padrão: .)',
    )
    parser.add_argument(
        "--encoding", default=None, help="codificação (padrão: utf-8, depois latin-1)"
    )
    parser.add_argument(
        "--agg",
        choices=["none", "sum", "mean", "median", "count"],
        default="none",
        help="agrupa linhas com a mesma categoria (padrão: none)",
    )
    parser.add_argument(
        "--max-barras",
        type=int,
        default=30,
        help="máximo de barras por gráfico (padrão: 30)",
    )
    parser.add_argument("--dpi", type=int, default=150, help="resolução (padrão: 150)")
    args = parser.parse_args()

    caminho = Path(args.arquivo)
    if not caminho.is_file():
        sys.exit(f"Erro: arquivo '{caminho}' não encontrado.")

    df = ler_csv(caminho, args.sep, args.decimal, args.encoding)
    df.columns = [str(c).strip() for c in df.columns]

    if df.empty:
        sys.exit("Erro: o CSV não contém dados.")

    # Colunas numéricas a plotar
    if args.y:
        faltando = [c for c in args.y if c not in df.columns]
        if faltando:
            sys.exit(f"Erro: coluna(s) não encontrada(s): {', '.join(faltando)}")
        colunas_y = args.y
        for c in colunas_y:
            df[c] = pd.to_numeric(df[c], errors="coerce")
    else:
        colunas_y = df.select_dtypes(include="number").columns.tolist()

    # Coluna de categorias (eixo X)
    if args.x:
        if args.x not in df.columns:
            sys.exit(f"Erro: coluna '{args.x}' não encontrada.")
        coluna_x = args.x
    else:
        candidatas = [
            c
            for c in df.columns
            if c not in colunas_y and not pd.api.types.is_numeric_dtype(df[c])
        ]
        coluna_x = candidatas[0] if candidatas else None

    if coluna_x is None:
        df["linha"] = range(1, len(df) + 1)
        coluna_x = "linha"

    colunas_y = [c for c in colunas_y if c != coluna_x]
    if not colunas_y:
        sys.exit("Erro: nenhuma coluna numérica encontrada para plotar.")

    pasta = Path(args.saida)
    pasta.mkdir(parents=True, exist_ok=True)

    for coluna in colunas_y:
        dados = df[[coluna_x, coluna]].dropna()
        if args.agg != "none":
            serie = dados.groupby(coluna_x)[coluna].agg(args.agg)
        else:
            serie = dados.set_index(coluna_x)[coluna]

        if serie.empty:
            print(f"Aviso: '{coluna}' não tem dados válidos, ignorada.")
            continue

        if len(serie) > args.max_barras:
            print(
                f"Aviso: '{coluna}' tem {len(serie)} categorias; "
                f"mostrando as primeiras {args.max_barras}."
            )
            serie = serie.head(args.max_barras)

        destino = pasta / f"{nome_seguro(coluna)}.png"
        gerar_grafico(serie, coluna_x, coluna, destino, args.dpi)
        print(f"Gerado: {destino}")


if __name__ == "__main__":
    main()
