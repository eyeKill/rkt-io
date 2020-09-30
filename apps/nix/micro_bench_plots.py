import sys
from typing import Any, Dict, List

import pandas as pd
from plot import apply_hatch, catplot
import os

from graph_utils import apply_aliases, change_width, column_alias


def iperf_zerocopy_plot(dir: str, graphs: List[Any]) -> None:
    df_all_on = pd.read_csv(
        os.path.join(os.path.realpath(dir), "iperf-all-on.tsv"), sep="\t"
    )

    df_zcopy_off = pd.read_csv(
        os.path.join(os.path.realpath(dir), "iperf-zerocopy-off.tsv"), sep="\t"
    )

    df_all_on = df_all_on[df_all_on["direction"] == "send"]
    df_zcopy_off = df_zcopy_off[df_zcopy_off["direction"] == "send"]

    df_all_on["iperf-throughput"] = df_all_on["bytes"] / df_all_on["seconds"] * 8 / 1e9
    df_zcopy_off["iperf-throughput"] = (
        df_zcopy_off["bytes"] / df_zcopy_off["seconds"] * 8 / 1e9
    )

    df_all_on["feature_dpdk"] = pd.Series(
        ["dpdk-zerocopy"] * len(df_all_on.index), index=df_all_on.index
    )
    df_zcopy_off["feature_dpdk"] = pd.Series(
        ["dpdk-copy"] * len(df_zcopy_off.index), index=df_zcopy_off.index
    )

    plot_df = pd.concat([df_all_on, df_zcopy_off], axis=0)
    plot_df = plot_df.reset_index(drop=True)

    g = catplot(
        data=apply_aliases(plot_df),
        x=column_alias("feature_dpdk"),
        y=column_alias("iperf-throughput"),
        kind="bar",
        height=2.5,
        legend=False,
        ci=None,
        color="black",
        palette=None,
    )

    change_width(g.ax, 0.25)
    g.ax.set_xlabel("")

    g.ax.set_xticklabels(g.ax.get_xmajorticklabels(), fontsize=6)
    g.ax.set_yticklabels(g.ax.get_ymajorticklabels(), fontsize=6)

    graphs.append(g)


def iperf_offload_plot(dir: str, graphs: List[Any]) -> None:
    df_all_on = pd.read_csv(
        os.path.join(os.path.realpath(dir), "iperf-all-on.tsv"), sep="\t"
    )

    df_zcopy_off = pd.read_csv(
        os.path.join(os.path.realpath(dir), "iperf-offload-off.tsv"), sep="\t"
    )

    df_all_on = df_all_on[df_all_on["direction"] == "send"]
    df_zcopy_off = df_zcopy_off[df_zcopy_off["direction"] == "send"]

    df_all_on["iperf-throughput"] = df_all_on["bytes"] / df_all_on["seconds"] * 8 / 1e9
    df_zcopy_off["iperf-throughput"] = (
        df_zcopy_off["bytes"] / df_zcopy_off["seconds"] * 8 / 1e9
    )

    df_all_on["feature_dpdk"] = pd.Series(
        ["dpdk-offload"] * len(df_all_on.index), index=df_all_on.index
    )
    df_zcopy_off["feature_dpdk"] = pd.Series(
        ["dpdk-no-offload"] * len(df_zcopy_off.index), index=df_zcopy_off.index
    )

    plot_df = pd.concat([df_all_on, df_zcopy_off], axis=0)
    plot_df = plot_df.reset_index(drop=True)

    g = catplot(
        data=apply_aliases(plot_df),
        x=column_alias("feature_dpdk"),
        y=column_alias("iperf-throughput"),
        kind="bar",
        height=2.5,
        legend=False,
        ci=None,
        color="black",
        palette=None,
    )

    change_width(g.ax, 0.25)
    g.ax.set_xlabel("")

    g.ax.set_xticklabels(g.ax.get_xmajorticklabels(), fontsize=6)
    g.ax.set_yticklabels(g.ax.get_ymajorticklabels(), fontsize=6)

    graphs.append(g)


def preprocess_hdparm(df_col: pd.Series) -> Any:
    df_col = list(df_col.values)
    for i in range(len(df_col)):
        # import pdb; pdb.set_trace()
        temp = df_col[i].split(" ")
        if temp[1] == "kB/s":
            df_col[i] = float(temp[0]) / (1000 ** 2)
        elif temp[1] == "MB/s":
            df_col[i] = float(temp[0]) / (1000)
        elif temp[1] == "GB/s":
            df_col[i] = float(temp[0])

    return pd.Series(df_col)


def hdparm_zerocopy_plot(dir: str, graphs: List[Any]) -> None:
    df_all_on = pd.read_csv(
        os.path.join(os.path.realpath(dir), "hdparm-all-on.tsv"), sep="\t"
    )

    df_zcopy_off = pd.read_csv(
        os.path.join(os.path.realpath(dir), "hdparm-zerocopy-off.tsv"), sep="\t"
    )

    df_all_on = df_all_on.drop(columns=["system"])
    df_zcopy_off = df_zcopy_off.drop(columns=["system"])

    df_all_on["Timing buffered disk reads"] = preprocess_hdparm(
        df_all_on["Timing buffered disk reads"],
    )
    df_zcopy_off["Timing buffered disk reads"] = preprocess_hdparm(
        df_zcopy_off["Timing buffered disk reads"],
    )

    df_all_on["Timing buffer-cache reads"] = preprocess_hdparm(
        df_all_on["Timing buffer-cache reads"],
    )

    df_zcopy_off["Timing buffer-cache reads"] = preprocess_hdparm(
        df_zcopy_off["Timing buffer-cache reads"],
    )

    df_all_on = df_all_on.T.reset_index()
    df_zcopy_off = df_zcopy_off.T.reset_index()

    df_zcopy_off.columns = ["hdparm_kind", "hdparm-throughput"]
    df_all_on.columns = ["hdparm_kind", "hdparm-throughput"]

    df_all_on["feature_spdk"] = pd.Series(
        ["spdk-zerocopy"] * len(df_all_on.index), index=df_all_on.index
    )
    df_zcopy_off["feature_spdk"] = pd.Series(
        ["spdk-copy"] * len(df_zcopy_off.index), index=df_zcopy_off.index
    )

    plot_df = pd.concat([df_all_on, df_zcopy_off], axis=0)
    groups = len(set(list(plot_df["feature_spdk"].values)))

    g = catplot(
        data=apply_aliases(plot_df),
        x=column_alias("feature_spdk"),
        y=column_alias("hdparm-throughput"),
        kind="bar",
        height=2.5,
        legend=False,
        hue=column_alias("hdparm_kind"),
    )

    apply_hatch(groups, g, True)
    change_width(g.ax, 0.25)
    g.ax.set_xlabel("")

    g.ax.set_xticklabels(g.ax.get_xmajorticklabels(), fontsize=6)
    g.ax.set_yticklabels(g.ax.get_ymajorticklabels(), fontsize=6)

    graphs.append(g)


def network_bs_plot(dir: str, graphs: List[Any]) -> None:
    df = pd.read_csv(
        os.path.join(os.path.realpath(dir), "network-test-bs-latest.tsv"), sep="\t"
    )
    df["network-bs-throughput"] = 1024 / df["time"]
    # df["batch_size"] = df["batch_size"].apply(lambda x: str(x)+"KiB")

    g = catplot(
        data=apply_aliases(df),
        x=column_alias("batch_size"),
        y=column_alias("network-bs-throughput"),
        kind="bar",
        height=2.5,
        legend=False,
        color="black",
        palette=None,
    )

    change_width(g.ax, 0.25)
    # g.ax.set_xlabel('')
    g.ax.set_xticklabels(g.ax.get_xmajorticklabels(), fontsize=6)
    g.ax.set_yticklabels(g.ax.get_ymajorticklabels(), fontsize=6)

    graphs.append(g)


def storage_bs_plot(dir: str, graphs: List[Any]) -> None:
    df = pd.read_csv(
        os.path.join(os.path.realpath(dir), "simpleio-unenc.tsv"), sep="\t"
    )
    df["storage-bs-throughput"] = (10 * 1024) / df["time"]

    g = catplot(
        data=apply_aliases(df),
        x=column_alias("batch-size"),
        y=column_alias("storage-bs-throughput"),
        kind="bar",
        height=2.5,
        legend=False,
        color="black",
        palette=None,
    )

    change_width(g.ax, 0.25)
    # g.ax.set_xlabel('')
    g.ax.set_xticklabels(g.ax.get_xmajorticklabels(), fontsize=6)
    g.ax.set_yticklabels(g.ax.get_ymajorticklabels(), fontsize=6)

    graphs.append(g)


def smp_plot(dir: str, graphs: List[Any]) -> None:
    df = pd.read_csv(
        os.path.join(os.path.realpath(dir), "smp-latest.tsv"), sep="\t"
    )
    df = pd.melt(df,
                 id_vars=['cores', 'job'],
                 value_vars=['read-bw', 'write-bw'],
                 var_name="operation",
                 value_name="disk-throughput")
    df = df.groupby(["cores", "operation"]).sum().reset_index()

    df["disk-throughput"] /= 1024
    g = catplot(
        data=apply_aliases(df),
        x=column_alias("cores"),
        y=column_alias("disk-throughput"),
        hue=column_alias("operation"),
        kind="bar",
        height=2.5,
        legend=False,
    )

    change_width(g.ax, 0.25)
    # g.ax.set_xlabel('')
    g.ax.set_xticklabels(g.ax.get_xmajorticklabels(), fontsize=6)
    g.ax.set_yticklabels(g.ax.get_ymajorticklabels(), fontsize=6)
    g.ax.legend(loc='best', fontsize='small')

    graphs.append(g)


def main() -> None:
    if len(sys.argv) < 1:
        sys.exit(1)

    graphs: List[Any] = []
    graph_names = []

    plot_func = {
        "dpdk_zerocopy": iperf_zerocopy_plot,
        "dpdk_offload": iperf_offload_plot,
        "spdk_zerocopy": hdparm_zerocopy_plot,
        "network_bs": network_bs_plot,
        "storage_bs": storage_bs_plot,
        "smp": smp_plot,
    }

    for name, pf in plot_func.items():
        pf(sys.argv[1], graphs)
        graph_names.append(name)

    for i in range(len(graphs)):
        name = f"{graph_names[i]}.pdf"
        print(name)
        graphs[i].savefig(name)


if __name__ == "__main__":
    main()
