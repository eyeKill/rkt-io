import csv
import sys
import os

import pandas as pd
from typing import Any
from plot import catplot, sns, plt, apply_hatch
from graph_utils import apply_aliases, column_alias, sort_systems


def fio_latency_graph(df: pd.DataFrame) -> Any:
    g = sns.FacetGrid(
        apply_aliases(df), row=column_alias("system"), col=column_alias("job")
    )
    g.map(plt.scatter, column_alias("clat_nsec"), column_alias("read_count"))
    g.add_legend()

    return g


def fio_iops_graph(df: pd.DataFrame) -> Any:
    df["iops"] = df["read-iops"] + df["write-iops"]
    g = catplot(
        data=apply_aliases(df),
        x=column_alias("system"),
        y=column_alias("iops"),
        hue=column_alias("job"),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )

    return g


def fio_read_graph(df: pd.DataFrame) -> Any:
    g = catplot(
        data=apply_aliases(df),
        x=column_alias("system"),
        y=column_alias("read-io_bytes"),
        hue=column_alias("job"),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )

    return g


def fio_write_graph(df: pd.DataFrame) -> Any:
    g = catplot(
        data=apply_aliases(df),
        x=column_alias("system"),
        y=column_alias("write-io_bytes"),
        hue=column_alias("job"),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )

    return g


def iperf_graph(df: pd.DataFrame) -> Any:
    df = df[df["direction"] == "send"]
    df["throughput"] = df["bytes"] / df["seconds"] * 8 / 1e9

    df = sort_systems(df)
    g = catplot(
        data=apply_aliases(df),
        x=column_alias("system"),
        y=column_alias("throughput"),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )
    groups = len(set((list(df["system"].values))))
    apply_hatch(groups, g, True)
    return g


def mysql_read_graph(df: pd.DataFrame) -> Any:
    g = catplot(
        data=apply_aliases(df),
        x=column_alias("system"),
        y=column_alias("SQL statistics read"),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )
    plt.legend(loc="lower right")
    return g


def mysql_write_graph(df: pd.DataFrame) -> Any:
    g = catplot(
        data=apply_aliases(df),
        x=column_alias("system"),
        y=column_alias("SQL statistics write"),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )
    plt.legend(loc="lower right")
    return g


def mysql_latency_graph(df: pd.DataFrame) -> Any:
    g = catplot(
        data=apply_aliases(df),
        x=column_alias("system"),
        y=column_alias("Latency (ms) avg"),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )
    plt.legend(loc="lower right")
    return g


def preprocess_hdparm(df_col: pd.Series) -> Any:
    df_col = list(df_col.values)
    for i in range(len(df_col)):
        temp = df_col[i].split(" ")
        if temp[1] == "kB/s":
            df_col[i] = float(df_col[i])/(1000 ** 2)
        elif temp[1] == "MB/s":
            df_col[i] = float(df_col[i])/(1000)
        elif temp[1] == "GB/s":
            df_col[i] = float(df_col[i])

    return pd.Series(df_col)


def hdparm_graph(df: pd.DataFrame, metric: str) -> Any:
    plot_col = ["system"]

    if metric == "cached":
        plot_col.append("Timing buffer-cache reads")
        df["Timing buffer-cache reads"] = preprocess_hdparm(
            df["Timing buffer-cache reads"],
        )
    elif metric == "buffered":
        plot_col.append("Timing buffered disk reads")
        df["Timing buffered disk reads"] = preprocess_hdparm(
            df["Timing buffered disk reads"],
        )

    plot_df = df[plot_col]
    plot_df = apply_aliases(plot_df)

    g = catplot(
        data=plot_df,
        x=plot_df.columns[0],
        y=plot_df.columns[1],
        kind="bar",
        height=2.5,
        aspect=1.2
    )

    return g


def memcpy_graph(df: pd.DataFrame) -> Any:
    g = catplot(
        data=apply_aliases(df),
        x=column_alias("memcpy-size"),
        y=column_alias("memcpy-time"),
        hue=column_alias("memcpy-kind"),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )

    return g


def print_usage() -> None:
    print(f"USAGE: {sys.argv[0]} results.tsv...", file=sys.stderr)
    sys.exit(1)


def read_shit(path: str) -> None:
    with open(path, "r") as csvfile:
        spamreader = csv.reader(csvfile, delimiter="\t")
        for row in spamreader:
            print(", ".join(row))


def main() -> None:
    if len(sys.argv) < 1:
        print_usage()

    graphs = []
    for arg in sys.argv[1:]:
        df = pd.read_csv(arg, delimiter="\t")
        basename = os.path.basename(arg)

        if basename.startswith("fio"):
            graphs.append(("FIO-IOPS", fio_iops_graph(df)))
            graphs.append(("FIO-READ", fio_read_graph(df)))
            graphs.append(("FIO-WRITE", fio_write_graph(df)))
        elif basename.startswith("mysql"):
            graphs.append(("MySQL-Reads", mysql_read_graph(df)))
            graphs.append(("MySQL-Writes", mysql_write_graph(df)))
            graphs.append(("MySQL-Latency", mysql_latency_graph(df)))
        elif basename.startswith("iperf"):
            graphs.append(("iperf", iperf_graph(df)))
        elif basename.startswith("hdparm"):
            graphs.append(("HDPARM-Cached", hdparm_graph(df, "cached")))
            graphs.append(("HDPARM-Buffered", hdparm_graph(df, "buffered")))
        elif basename.startswith("memcpy"):
            graphs.append(("MEMCPY", memcpy_graph(df)))

    for name, graph in graphs:
        filename = f"{name}.pdf"
        print(f"write {filename}")
        graph.savefig(filename)


if __name__ == "__main__":
    main()
