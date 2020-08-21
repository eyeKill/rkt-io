import sys

from helpers import create_settings
from network import Network, NetworkKind


def main() -> None:
    if len(sys.argv) < 2:
        print(f"USAGE: {sys.argv[0]} (native|dpdk|tap)", file=sys.stderr)
        sys.exit(1)
    kinds = dict(native=NetworkKind.NATIVE, dpdk=NetworkKind.DPDK, tap=NetworkKind.TAP)
    kind = kinds.get(sys.argv[1], None)
    if kind is None:
        print(
            f"Unsupported option '{sys.argv[1]}', valid options are native, dpdk or tap",
            file=sys.stderr,
        )
        sys.exit(1)
    settings = create_settings()
    Network(settings).setup(kind)


if __name__ == "__main__":
    main()
