import sys
from typing import Any, Dict, List

import pandas as pd
from plot import apply_hatch, catplot

from graph_utils import apply_aliases, change_width


def df_col_select(res_col: List[str], df_columns: List[str], keyword: str) -> None:
    for col in df_columns:
        if col in res_col:
            continue
        if keyword in col:
            res_col.append(col)

def apply_sqlite_rows(x: int) -> float:
    return 10000 / x


def sqlite_graph(df: pd.DataFrame) -> Any:
    plot_df = df
    df = df[df.columns[2]]
    df = df.map(apply_sqlite_rows)
    plot_df.update(df)

    plot_df["sqlite-op-type"] = plot_df["sqlite-op-type"].map(
        {
            "10000 INSERTs into table with no index": "Insert",
            "10000 UPDATES of individual rows": "Update",
            "10000 DELETEs of individual rows": "Delete",
        }
    )

    groups = len(set((list(plot_df["system"].values))))
    plot_df = apply_aliases(plot_df)

    g = catplot(
        data=plot_df,
        x=plot_df.columns[0],
        y=plot_df.columns[2],
        kind="bar",
        height=2.5,
        # aspect=0.8,
        order=systems_order(df),
        hue="Operation",
        legend=False,
        palette="Greys",
    )

    apply_hatch(groups, g, True)
    change_width(g.ax, 0.25)

    return g


def nginx_graph(df: pd.DataFrame, metric: str) -> Any:
    plot_col = ["system"]

    if metric == "lat":
        plot_col.append("lat_avg(ms)")
    elif metric == "thru":
        plot_col.append("req_sec_tot")

    plot_df = df[plot_col]
    groups = len(set((list(plot_df["system"].values))))
    plot_df = apply_aliases(plot_df)

    g = catplot(
        data=plot_df,
        x=plot_df.columns[0],
        y=plot_df.columns[1],
        kind="bar",
        order=systems_order(df),
        height=2.5,
        # aspect=1.2,
    )
    apply_hatch(groups, g, False)
    change_width(g.ax, 0.25)
    return g


def redis_graph(df: pd.DataFrame, metric: str) -> Any:
    df_flag = None
    hue = None
    col_name = None
    legend = False

    if metric == "thru":
        df_flag = df["metric"] == "Throughput(ops/sec)"
        col_name = "Throughput(ops/sec)"
        legend = False
    elif metric == "lat":
        df_flag = (df["metric"] == "AverageLatency(us)") & (
            df["operation"] != "[CLEANUP]"
        )
        col_name = "AverageLatency(us)"
        hue = "operation"
        legend = True

    plot_df = df[df_flag]
    groups = len(set((list(plot_df["system"].values))))
    plot_df = plot_df.drop(["metric"], axis=1)
    plot_df = plot_df.rename(columns={"value": col_name})
    plot_df = apply_aliases(plot_df)

    g = catplot(
        data=plot_df,
        x=plot_df.columns[0],
        y=plot_df.columns[-1],
        hue=hue,
        kind="bar",
        height=2.5,
        order=systems_order(df),
        # aspect=1.2,
        legend=False,
    )

    apply_hatch(groups, g, legend)
    change_width(g.ax, 0.25)

    return g


def print_usage() -> None:
    pass


def main() -> None:
    if len(sys.argv) < 1:
        print_usage()
        sys.exit(1)

    graphs = []
    for arg in sys.argv[1:]:
        df = pd.read_csv(arg, delimiter="\t")

        if arg.startswith("sqlite"):
            graphs.append(("SQLITE", sqlite_graph(df)))
        if arg.startswith("nginx"):
            graphs.append(("NGINX-LAT", nginx_graph(df, "lat")))
            graphs.append(("NGINX-THRU", nginx_graph(df, "thru")))
        if arg.startswith("redis"):
            graphs.append(("REDIS-THRU", redis_graph(df, "thru")))
            graphs.append(("REDIS-LAT", redis_graph(df, "lat")))

    for name, graph in graphs:
        filename = f"{name}.pdf"
        print(f"write {filename}")
        graph.savefig(filename)


if __name__ == "__main__":
    main()
