# workaround to select Agg as backend consistenly
import matplotlib as mpl
import matplotlib.pyplot as plt
import seaborn as sns

mpl.use("Agg")
mpl.rcParams["text.latex.preamble"] = [r"\usepackage{amsmath}"]
mpl.rcParams["pdf.fonttype"] = 42
mpl.rcParams["ps.fonttype"] = 42

# plt.rc('text', usetex=True)
plt.figure(figsize=(2.5, 2.5))
sns.set_style("whitegrid")
sns.set_context(font_scale=1.5)
sns.set_palette(sns.color_palette(palette="gray", n_colors=2))


def rescale_barplot_width(ax, factor=0.6):
    for bar in ax.patches:
        x = bar.get_x()
        new_width = bar.get_width() * factor
        center = x + bar.get_width() / 2.
        bar.set_width(new_width)
        bar.set_x(center - new_width / 2.)


def alternate_bar_color(graph):
    for i, patch in enumerate(graph.axes[0][0].patches):
        if i % 2 == 0:
            patch.set_facecolor(
                (0.3333333333333333, 0.3333333333333333, 0.3333333333333333, 1.0)
            )
        else:
            patch.set_facecolor(
                (0.6666666666666666, 0.6666666666666666, 0.6666666666666666, 1.0)
            )
