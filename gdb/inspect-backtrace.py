import sys
import pandas as pd


def main():
    if len(sys.argv) < 2:
        print(f"USAGE: {sys.argv[0]} file")
        sys.exit(1)
    df = pd.read_csv(sys.argv[1])

    unscheduled = df[~df.backtrace.str.contains("__schedule")]
    for _, row in unscheduled.iterrows():
        print(row.backtrace)

    breakpoint()

    for idx, row in df.iterrows():
        print(row.backtrace)
        traces = list(df.backtrace)

    print(traces[0])

if __name__ == '__main__':
    main()
