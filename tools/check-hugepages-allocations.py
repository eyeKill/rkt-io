#!/usr/bin/env python

import sys
import os


def check_hugepages_are_continous(path: str) -> None:
    previous = None
    previous_cols = None
    gaps = 0
    with open(path) as f:
        buf = f.read()
        lines = buf.split("\n")
        for line in lines:
            columns = line.split()
            if len(columns) < 6:
                continue
            if columns[5].startswith("/dev/hugepages"):
                lower, upper = columns[0].split("-")
                if previous is not None:
                    if lower != previous:
                        print(f"The following two allocations have a gap: {previous_cols[5]} <-> {columns[5]}!")
                        gaps += 1
                previous = upper
                previous_cols = columns
                #print(columns[0])
    if gaps == 0:
        print("No gaps found")

def main():
    if len(sys.argv) < 2:
        print("USAGE: PID_OR_MAPS_FILE", file=sys.stderr)
        sys.exit(1)
    if os.path.exists(sys.argv[1]):
        path = sys.argv[1]
    else:
        path = int(sys.argv[1])
    check_hugepages_are_continous(path)


if __name__ == "__main__":
    main()
