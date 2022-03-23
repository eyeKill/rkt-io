import sys

from helpers import create_settings
from network import Network, NetworkKind


def main() -> None:
    if len(sys.argv) < 2:
        print(f"USAGE: {sys.argv[0]} (native|dpdk|tap|client-native)", file=sys.stderr)
        sys.exit(1)
    kinds = {
        "native": NetworkKind.NATIVE,
        "client-native": NetworkKind.CLIENT_NATIVE,
        "dpdk": NetworkKind.DPDK,
        "dpdk-tap": NetworkKind.DPDK_TAP,
        "tap": NetworkKind.TAP,
    }
    kind = kinds.get(sys.argv[1], None)
    if kind is None:
        print(
            f"Unsupported option '{sys.argv[1]}', valid options are native, client-native, dpdk or tap",
            file=sys.stderr,
        )
        sys.exit(1)
    settings = create_settings()
    Network(settings).setup(kind)


if __name__ == "__main__":
    main()
