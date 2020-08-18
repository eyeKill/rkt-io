import csv
import sys
from typing import Any, Dict

import pandas as pd
from plot import plt, sns

SYSTEM_ALIASES: Dict[str, str] = {}
ROW_ALIASES = dict(system=SYSTEM_ALIASES)
COLUMN_ALIASES: Dict[str, str] = {
    "throughput": "Throughput [GiB/s]",
    "SQL statistics read": "Read",
    "SQL statistics write": "Write",
    "Latency (ms) avg": "Latency [ms]",
}


def column_alias(name: str) -> str:
    return COLUMN_ALIASES.get(name, name)


def catplot(**kwargs: Any) -> Any:
    g = sns.catplot(**kwargs)
    g.despine(top=False, right=False)
    plt.autoscale()
    plt.subplots_adjust(top=0.98)
    return g


def apply_aliases(df: pd.DataFrame) -> pd.DataFrame:
    for column in df.columns:
        aliases = ROW_ALIASES.get(column, None)
        if aliases is not None:
            df[column] = df[column].replace(aliases)
    return df.rename(index=str, columns=COLUMN_ALIASES)


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
        y=column_alias("read-iobytes"),
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
        y=column_alias("write-iobytes"),
        hue=column_alias("job"),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )

    return g


def iperf_graph(df: pd.DataFrame) -> Any:
    df["throughput"] = df["bytes"] / df["seconds"] * 8 / 1e9

    g = catplot(
        data=apply_aliases(df),
        x=column_alias("system"),
        y=column_alias("throughput"),
        hue=column_alias("direction"),
        kind="bar",
        height=2.5,
        aspect=1.2,
    )
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

        if arg.startswith("fio-throughput"):
            graphs.append(("FIO-IOPS", fio_iops_graph(df)))
            graphs.append(("FIO-READ", fio_read_graph(df)))
            graphs.append(("FIO-WRITE", fio_write_graph(df)))
        elif arg.startswith("fio-latency"):
            graphs.append(("FIO", fio_latency_graph(df)))
        elif arg.startswith("mysql"):
            graphs.append(("MySQL-Reads", mysql_read_graph(df)))
            graphs.append(("MySQL-Writes", mysql_write_graph(df)))
            graphs.append(("MySQL-Latency", mysql_latency_graph(df)))
        elif arg.startswith("iperf"):
            graphs.append(("Iperf", iperf_graph(df)))

    for name, graph in graphs:
        filename = f"{name}.png"
        print(f"write {filename}")
        graph.savefig(filename)


if __name__ == "__main__":
    main()
