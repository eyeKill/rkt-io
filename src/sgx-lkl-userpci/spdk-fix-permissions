#!/bin/sh

#set -eux
chown $1 $(awk '/hugetlbfs/ {print $2}' /proc/mounts)/rtemap*
chown $1 /var/run/dpdk/rte/{,config,fbarray_*,hugepage_info} /var/run/dpdk
chown $1 /var/run/dpdk/rte/mp_socket
