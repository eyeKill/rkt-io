import sys
from typing import Any, Dict, List

import pandas as pd
from plot import plt, sns

SYSTEM_ALIASES: Dict[str, str] = {}
ROW_ALIASES = dict(system=SYSTEM_ALIASES)
COLUMN_ALIASES: Dict[str, str] = {
    "1000000 INSERTs into table with no index": "insert [s]",
    "25 SELECTS, numeric BETWEEN, unindexed": "Numeric select [s]",
    "1000000 UPDATES of individual rows": "update [s]",
    "lat_avg": "Latency [ms]",
    "req_sec_tot": "Requests/sec",
    "Throughput(ops/sec)": "Throughput",
    "AverageLatency(ms)": "Latency [ms]"
}

def catplot(**kwargs) -> Any:
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

def df_col_select(res_col:List[str], df_columns: List[str], keyword: str) -> None:
    for col in df_columns:
        if col in res_col:
            continue
        if keyword in col:
            res_col.append(col)

def sqlite_graph(df: pd.DataFrame, query_kind: str) -> Any:
    plot_col = ["system"]

    if query_kind=="insert":
        df_col_select(plot_col, df.columns, "INSERT")
    if query_kind=="25-select":
        df_col_select(plot_col, df.columns, "25 SELECTS")
    if query_kind=="all-update":
        df_col_select(plot_col, df.columns, "UPDATES")

    plot_df = df[plot_col]
    plot_df = apply_aliases(plot_df)

    g = catplot(
            data=plot_df,
            x=plot_df.columns[0],
            y=plot_df.columns[1],
            kind="bar",
            height=2.5,
            aspect=1.2,
        )
    return g

def nginx_graph(df: pd.DataFrame, metric: str) -> Any:
    plot_col = ["system"]

    if metric == "lat":
        plot_col.append("lat_avg")
    elif metric == "thru":
        plot_col.append("req_sec_tot")

    plot_df = df[plot_col]
    plot_df = apply_aliases(plot_df)

    g = catplot(
            data=plot_df,
            x=plot_df.columns[0],
            y=plot_df.columns[1],
            kind="bar",
            height=2.5,
            aspect=1.2,
        )
    return g

def redis_graph(df: pd.DataFrame, metric: str) -> Any:
    df_flag = None
    hue = None
    col_name = None

    if metric == "thru":
        df_flag = (df["metric"] == "Throughput(ops/sec)")
        col_name = "Throughput(ops/sec)"
    elif metric == "lat":
        df_flag = (df["metric"] == "AverageLatency(ms)")
        col_name = "AverageLatency(ms)"
        hue = "operation"

    plot_df = df[df_flag]
    plot_df = plot_df.drop(["metric"], axis=1)
    plot_df = plot_df.rename(columns={"value":col_name})
    plot_df = apply_aliases(plot_df)

    g = catplot(
            data=plot_df,
            x=plot_df.columns[0],
            y=plot_df.columns[-1],
            hue=hue,
            kind="bar",
            height=2.5,
            aspect=1.2,
        )
    return g


def print_usage() -> None:
    pass

def main() -> None:
    if len(sys.argv) < 1:
        print_usage()
        sys.exit(1)

    graphs = []
    for arg in sys.argv[1:]:
        df = pd.read_csv(arg, delimiter='\t')

        if arg.startswith("sqlite"):
            graphs.append(("SQLITE-INSERT", sqlite_graph(df, "insert")))
            graphs.append(("SQLITE-25-NUMERIC-SELECT", sqlite_graph(df, "25-select")))
            graphs.append(("SQLITE-ALL-ROW-UPDATE", sqlite_graph(df, "all-update")))
        if arg.startswith("nginx"):
            graphs.append(("NGINX-LAT", nginx_graph(df, "lat")))
            graphs.append(("NGINX-THRU", nginx_graph(df, "thru")))
        if arg.startswith("redis"):
            graphs.append(("REDIS-THRU", redis_graph(df, "thru")))
            graphs.append(("REDIS-LAT", redis_graph(df, "lat")))

    for name, graph in graphs:
        filename = f"{name}.png"
        print(f"write {filename}")
        graph.savefig(filename)

if __name__=="__main__":
    main()
