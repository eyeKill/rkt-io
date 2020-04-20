#!/usr/bin/env python3
import glob
import os


def dump_files(dir_path: str):
    print(dir_path)
    for tuneable in os.listdir(dir_path):
        path = os.path.join(dir_path, tuneable)
        if os.path.isdir(path):
            dump_files(path)
        else:
            try:
                with open(path) as file:
                    print(f"{tuneable}: {file.read()}", end="")
            except OSError:
                pass


def main() -> None:
    for queue in glob.glob("/sys/block/*/queue"):
        dump_files(queue)


if __name__ == '__main__':
    main()
