# RKT-IO

Rkt-io direct userspace network and storage I/O stack
specifically designed for TEEs that combines high-performance, POSIX
compatibility and security. rkt-io achieves high I/O performance by employing
direct userspace I/O libraries (DPDK and SPDK) inside the TEE for kernel-bypass
I/O. For efficiency, rkt-io polls for I/O events directly interacting with the
hardware instead of relying on interrupts, and it avoids data copies by mapping
DMA regions in the untrusted host memory.  To maintain full Linux ABI compatibil
ity, the userspace I/O libraries are integrated with userspace versions of the
Linux VFS and network stacks inside the TEE.  Since the I/O stack runs entirely
within the TEE, thus omitting the host OS from the I/O path, rkt-io can
transparently encrypt all I/O data and does not suffer from host interface/Iago
attacks.

## Usage

rkt-io is a fork of [sgx-lkl](https://github.com/lsds/sgx-lkl). For normal usage
see the old [README](README.old.md). For reducing the paper results read the
next headline.

## Reproduce paper results 

### For Eurosys evaluation testers

Due its special hardware requirments we provide ssh access to our evaluation
machines. Please contact the paper author email address to obtain ssh keys. The
machines will have the correct hardware and also software installed to run the
experiments. If you run into problems you can write join the IRC channel
#rkt-io on freenode fro a live chat (there is also a webchat version at
https://webchat.freenode.net/) or write an email for further questions.

### Hardware

- Intel NIC supported by i40e driver: In rkt-io we performed some driver
  optimizations that required some refactorings in DPDK to reduce memory copy.
   Hence we had to modify the low-level i40e intel NIC driver. We did not apply
  those refactorings to other drivers. Hence one needs the same hardware to
  reproduce the paper results. Our NIC was [XL710](https://www.intel.com/content/www/us/en/products/docs/network-io/ethernet/network-adapters/ethernet-xl710-brief.html)
- NVME block device: We need a free NVME block device. During evaluation this
  device will be reformated. We used an [Intel DC P4600 2TB](https://ark.intel.com/content/www/us/en/ark/products/series/96947/intel-ssd-dc-p4600-series.html)
  NVME drive.
- Intel CPU with SGX support: Most new consumer CPUs have SGX support. Some
  server xeon processors don't
- A second machine acting as a client. This one needs a similar capable NIC (i.e. same bandwith).
  The other machine does not need to have an NVME drive.

### Software
- Linux
- [Nix](https://nixos.org/download.html): For reproducibility we use the nix
package manager to download all build dependencies. We locked the package
versions to ensure reproducibility so that. On our evaluations machines we 
- Python 3.7 or newer: We wrapped the reproduction script in a python script.

### Run evaluation

The first step is to get the source code for rkt-io:

```console
$ git clone https://github.com/Mic92/sgx-lkl
```

For convience we created an evaluation script (reproduce.py) that will first build rkt-io and
than run all evaluation experiments from the paper.
This script only depends on Python and Nix as referenced above. 
All other dependencies will be loaded through nix.
If the script fails at any point it can be restarted and it will
only not yet done builds or experiments.
Each command it runs will be printed to during evaluation along with 
environment variable set. 
In addition to some default settings also machine specific settings are
required. The script read those from a file containing the hostname of the
machine + `.env`. An example configuration file is provided in the repo
(martha.env - @eurosys testers - you don't need to change anything).

To run the evaluation script use the following command:
```console
$ python reproduce.py 
```

- Figure 1. Micro-benchmarks to showcase the performance of syscalls, storage and network stacks across different systems
  a) System call latency with sendto()
  b) Storage stack performance with fio
  c) Network stack performance with iPerf
  
- Figure 5. Micro-benchmarks to showcase the effectiveness of various design choices in rkt-io Effectiveness of the SMP design w/ fio with increasing number of threads
  a) Effectiveness of the SMP design w/ fio with increasing number of threads
  b) iPerf throughput w/ different optimizations
  c) Effectiveness of hardware-accelerated crypto routines
  
- Figure 7. The above plots compare the performance of four real-world
  applications (SQlite, Ngnix, Redis, and MySQL) while running atop native linux
  a) SQLite throughput w/ Speedtest (no security) and three secure systems: Scone, SGX-LKL and rkt-io
  b) Nginx latency w/ wrk
  c) Nginx throughput w/ wrk
  d) Redis throughput w/ YCSB (A)
  e) Redis latency w/ YCSB (A)
  f) MySQL OLTP throughput w/ sys-bench
