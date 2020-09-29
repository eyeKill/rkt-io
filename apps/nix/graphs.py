import csv
import sys
import os

import pandas as pd
from typing import Any
from plot import catplot, plt, apply_hatch
from graph_utils import apply_aliases, column_alias, systems_order, change_width


def fio_read_write_graph(df: pd.DataFrame) -> Any:
    df = pd.melt(df,
                 id_vars=['system', 'job'],
                 value_vars=['read-bw', 'write-bw'],
                 var_name="operation",
                 value_name="disk-throughput")
    df = df.groupby(["system", "operation"]).sum().reset_index()

    df["disk-throughput"] /= 1024
    g = catplot(
        data=apply_aliases(df),
        x=column_alias("system"),
        y=column_alias("disk-throughput"),
        hue=column_alias("operation"),
        order=systems_order(df),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )

    return g


def syscalls_perf_graph(df: pd.DataFrame) -> Any:
    df2 = df[(df.data_size == 32) & (df.threads == 8)]
    df2 = df2.assign(time_per_syscall=10e6 * df2.total_time / (df2.packets_per_thread * df2.threads))
    g = catplot(
        data=apply_aliases(df2),
        x=column_alias("system"),
        y=column_alias("time_per_syscall"),
        order=systems_order(df2),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )

    return g


def iperf_graph(df: pd.DataFrame) -> Any:
    df = df[df["direction"] == "send"]
    df["throughput"] = df["bytes"] / df["seconds"] * 8 / 1e9

    g = catplot(
        data=apply_aliases(df),
        x=column_alias("system"),
        y=column_alias("throughput"),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )
    return g


def mysql_read_graph(df: pd.DataFrame) -> Any:
    groups = len(set((list(df["system"].values))))

    g = catplot(
        data=apply_aliases(df),
        x=column_alias("system"),
        y=column_alias("SQL statistics read"),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )
    apply_hatch(groups, g, False)
    change_width(g.ax, 0.25)
    return g


def mysql_write_graph(df: pd.DataFrame) -> Any:
    groups = len(set((list(df["system"].values))))

    g = catplot(
        data=apply_aliases(df),
        x=column_alias("system"),
        y=column_alias("SQL statistics write"),
        kind="bar",
        height=2.5,
        # aspect=1.2,
    )
    apply_hatch(groups, g, False)
    change_width(g.ax, 0.25)
    return g


def mysql_latency_graph(df: pd.DataFrame) -> Any:
    groups = len(set((list(df["system"].values))))

    g = catplot(
        data=apply_aliases(df),
        x=column_alias("system"),
        y=column_alias("Latency (ms) avg"),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )
    apply_hatch(groups, g, False)
    change_width(g.ax, 0.25)
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
            graphs.append(("fio-read-write", fio_read_write_graph(df)))
        if basename.startswith("syscalls-perf"):
            graphs.append(("syscalls-perf", syscalls_perf_graph(df)))
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
