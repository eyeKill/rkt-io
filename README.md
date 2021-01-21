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

rkt-io is a fork of [sgx-lkl](https://github.com/lsds/sgx-lkl). For usage see
the old [README](README.old.md).

## Reproduce paper results 

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

### Software

- [Nix](https://nixos.org/download.html): For reproducibility we use the nix
package manager to download all build dependencies. We locked the package
versions to ensure reproducibility so that.
