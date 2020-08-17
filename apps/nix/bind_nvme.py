import sys

from helpers import create_settings
from storage import Storage, StorageKind


def main() -> None:
    if len(sys.argv) < 2:
        print(f"USAGE: {sys.argv[0]} (native|spdk|lkl)", file=sys.stderr)
        sys.exit(1)
    kinds = dict(native=StorageKind.NATIVE,
                 spdk=StorageKind.SPDK,
                 lkl=StorageKind.LKL,
                 scone=StorageKind.SCONE)
    kind = kinds.get(sys.argv[1], None)
    if kind is None:
        print(
            f"Unsupported option '{sys.argv[1]}', valid options are native, spdk or lkl",
            file=sys.stderr,
        )
        sys.exit(1)
    settings = create_settings()
    mount = Storage(settings).setup(kind)
    print(mount.dev)


if __name__ == "__main__":
    main()
