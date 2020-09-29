import sys
from typing import Any, Dict, List

import pandas as pd
from plot import apply_hatch, catplot
import os

from graph_utils import apply_aliases, change_width, column_alias

def iperf_zerocopy_plot(dir, graphs):
	df_all_on = pd.read_csv(
			os.path.join(os.path.realpath(dir), "iperf-all-on.tsv"),
			sep='\t'
		)

	df_zcopy_off = pd.read_csv(
			os.path.join(os.path.realpath(dir), "iperf-zerocopy-off.tsv"),
			sep='\t'
		)

	df_all_on = df_all_on[df_all_on["direction"] == "send"]
	df_zcopy_off = df_zcopy_off[df_zcopy_off["direction"] == "send"]

	df_all_on["iperf-throughput"] = df_all_on["bytes"] / df_all_on["seconds"] * 8 / 1e9
	df_zcopy_off["iperf-throughput"] = df_zcopy_off["bytes"] / df_zcopy_off["seconds"] * 8 / 1e9

	df_all_on["feature_dpdk"] = pd.Series(["dpdk-zerocopy"]*len(df_all_on.index), index=df_all_on.index)
	df_zcopy_off["feature_dpdk"] = pd.Series(["dpdk-copy"]*len(df_zcopy_off.index), index=df_zcopy_off.index)

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
		)

	change_width(g.ax, 0.25)
	g.ax.set_xlabel('')

	graphs.append(g)

def iperf_offload_plot(dir, graphs):
	df_all_on = pd.read_csv(
			os.path.join(os.path.realpath(dir), "iperf-all-on.tsv"),
			sep='\t'
		)

	df_zcopy_off = pd.read_csv(
			os.path.join(os.path.realpath(dir), "iperf-offload-off.tsv"),
			sep='\t'
		)

	df_all_on = df_all_on[df_all_on["direction"] == "send"]
	df_zcopy_off = df_zcopy_off[df_zcopy_off["direction"] == "send"]

	df_all_on["iperf-throughput"] = df_all_on["bytes"] / df_all_on["seconds"] * 8 / 1e9
	df_zcopy_off["iperf-throughput"] = df_zcopy_off["bytes"] / df_zcopy_off["seconds"] * 8 / 1e9

	df_all_on["feature_dpdk"] = pd.Series(["dpdk-offload"]*len(df_all_on.index), index=df_all_on.index)
	df_zcopy_off["feature_dpdk"] = pd.Series(["dpdk-no-offload"]*len(df_zcopy_off.index), index=df_zcopy_off.index)

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
		)

	change_width(g.ax, 0.25)
	g.ax.set_xlabel('')

	graphs.append(g)

def hdparm_zerocopy_plot(dir, graphs):
	pass


def main():
	if len(sys.argv) < 1:
		sys.exit(1)

	graphs = []
	graph_names = []
	
	plot_func = {
		"dpdk_zerocopy":iperf_zerocopy_plot,
		"dpdk_offload": iperf_offload_plot,
		"spdk_zerocopy": hdparm_zerocopy_plot,
	}

	for name, pf in plot_func.items():
		pf(sys.argv[1], graphs)
		graph_names.append(name)

	for i in range(len(graphs)):
		graphs[i].savefig(f"{graph_names[i]}.pdf")

if __name__=="__main__":
	main()