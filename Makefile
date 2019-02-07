include config.mak

.PHONY: host-musl lkl sgx-lkl-musl-config sgx-lkl-musl sgx-lkl tools clean enclave-debug-key \
	numactl numactl-autogen numactl-config dpdk compdb

# boot memory reserved for LKL/kernel (in MB)
BOOT_MEM=12 # Default in LKL is 64

# Max. number of enclave threads/TCS
NUM_TCS=8

HW_MODE=yes

default: all

# Default is to build everything
all: sgx-lkl-musl sgx-lkl

sim: HW_MODE=no
sim: all

MAKE_ROOT=$(dir $(realpath $(firstword $(MAKEFILE_LIST))))

# Vanilla Musl compiler
host-musl ${HOST_MUSL_CC}: | ${HOST_MUSL}/.git ${HOST_MUSL_BUILD}
	cd ${HOST_MUSL}; [ -f config.mak ] || CFLAGS="$(MUSL_CFLAGS)" ./configure \
		$(MUSL_CONFIGURE_OPTS) \
		--prefix=${HOST_MUSL_BUILD}
	+${MAKE} -j`tools/ncore.sh` -C ${HOST_MUSL} CFLAGS="$(MUSL_CFLAGS)" install
	ln -fs ${LINUX_HEADERS_INC}/linux/ ${HOST_MUSL_BUILD}/include/linux
	ln -fs ${LINUX_HEADERS_INC}/asm-generic/ ${HOST_MUSL_BUILD}/include/asm
	ln -fs ${LINUX_HEADERS_INC}/asm-generic/ ${HOST_MUSL_BUILD}/include/asm-generic
	# Fix musl-gcc for gcc version that have been built with --enable-default-pie
	gcc -v 2>&1 | grep "\-\-enable-default-pie" > /dev/null && sed -i 's/"$$@"/-fpie -pie "\$$@"/g' ${HOST_MUSL_BUILD}/bin/musl-gcc || true

numactl-autogen: | ${NUMACTL}.git
	cd ${NUMACTL}; [ -f configure ] || ./autogen.sh

numactl-config: numactl-autogen ${HOST_MUSL_CC}
	+cd ${NUMACTL}; [ -f Makefile ] || \
		CFLAGS="$(MUSL_CFLAGS)" CC=${HOST_MUSL_CC} ./configure --disable-shared --prefix=${NUMACTL_BUILD}

numactl-patch ${NUMACTL}/.patched:
  # numactl uses symbol versioning and requires version scripts when beeing linked as a static library
  # into libsgxlkl.so. Since their scripst mess up our exported symbols, we simply assume the latest version.
	sed -i -e 's!__asm__.*symver.*!!g;s!_v2!!g' ${NUMACTL}/*.c
	touch ${NUMACTL}/.patched

numactl: ${NUMACTL}/.patched numactl-config
	make -C ${NUMACTL} -j$(nproc) install

DPDK_VERSION = 17.02
RTE_TARGET = x86_64-native-linuxapp-gcc
DPDK_CONFIG = ${DPDK_BUILD}/.config
DPDK_FLAGS = -Wno-error \
		-Wno-error=implicit-function-declaration \
		-Wno-error=nested-externs \
		-Wno-error=implicit-fallthrough \
		-Wno-error=pointer-to-int-cast

# when compiling with BEAR the build seems to fail at some point also the overall build is still fine
DPDK_BEAR_HACK ?= no

dpdk-config ${DPDK_CONFIG}: ${CURDIR}/src/dpdk/override/defconfig
	make -j1 -C ${DPDK} PATH=${NUMACTL_BUILD}/bin:$$PATH RTE_SDK=${DPDK} T=${RTE_TARGET} O=${DPDK_BUILD} config
	cat ${DPDK_BUILD}/.config.orig ${CURDIR}/src/dpdk/override/defconfig > ${DPDK_BUILD}/.config

dpdk ${DPDK_BUILD}/lib/librte_pmd_ixgbe.a: ${DPDK_CONFIG} ${HOST_MUSL_CC} numactl | ${DPDK}/.git ${RTE_SDK}
	+make -j`tools/ncore.sh` -C ${DPDK_BUILD} WERROR_FLAGS= CC=${HOST_MUSL_CC} RTE_SDK=${DPDK} V=1 \
		EXTRA_CFLAGS="$(MUSL_CFLAGS) -UDEBUG -lc ${DPDK_FLAGS} -I${NUMACTL_BUILD}/include -include ${CURDIR}/src/dpdk/override/uint.h" \
		EXTRA_LDFLAGS="-L${NUMACTL_BUILD}/lib" || test ${DPDK_BEAR_HACK} == "yes"

# Since load-dpdk-driver may require root,
# we don't want to build the kernel modules here and
# ask the user to run make instead
${DPDK_BUILD}/kmod/%.ko:
	$(error "Kernel module $@ not found, run `make dpdk` first")

load-dpdk-driver: ${DPDK_BUILD}/kmod/igb_uio.ko ${DPDK_BUILD}/kmod/rte_kni.ko
	rmmod rte_kni || true
	rmmod igb_uio || true
	insmod ${DPDK_BUILD}/kmod/rte_kni.ko
	insmod ${DPDK_BUILD}/kmod/igb_uio.ko

fix-dpkg-permissions:
	sudo chown `id -u` \
		/mnt/huge /dev/uio* /sys/class/uio/uio*/device/{config,resource*} \

# LKL's static library and include/ header directory
lkl ${LIBLKL}: ${DPDK_BUILD}/lib/librte_pmd_ixgbe.a ${HOST_MUSL_CC} | ${LKL}/.git ${LKL_BUILD} src/lkl/override/defconfig
	# Override lkl's defconfig with our own
	cp -Rv src/lkl/override/defconfig ${LKL}/arch/lkl/defconfig
	cp -Rv src/lkl/override/include/uapi/asm-generic/stat.h ${LKL}/include/uapi/asm-generic/stat.h
	# Set bootmem size (default in LKL is 64MB)
	sed -i 's/static unsigned long mem_size = .*;/static unsigned long mem_size = ${BOOT_MEM} \* 1024 \* 1024;/g' lkl/arch/lkl/kernel/setup.c
	# Disable loading of kernel symbols for debugging/panics
	grep -q -F 'CONFIG_KALLSYMS=n' ${LKL}/arch/lkl/defconfig || echo 'CONFIG_KALLSYMS=n' >> ${LKL}/arch/lkl/defconfig
	+DESTDIR=${LKL_BUILD} ${MAKE} dpdk=yes V=1 RTE_SDK=${DPDK_BUILD} RTE_TARGET= -C ${LKL}/tools/lkl -j`tools/ncore.sh` CC=${HOST_MUSL_CC} PREFIX="" \
		${LKL}/tools/lkl/liblkl.a
	mkdir -p ${LKL_BUILD}/lib
	cp ${LKL}/tools/lkl/liblkl.a $(LKL_BUILD)/lib
	+DESTDIR=${LKL_BUILD} ${MAKE} dpdk=yes -C ${LKL}/tools/lkl -j`tools/ncore.sh` CC=${HOST_MUSL_CC} PREFIX="" \
		TARGETS="" headers_install
	# Bugfix, prefix symbol that collides with musl's one
	find ${LKL_BUILD}/include/ -type f -exec sed -i 's/struct ipc_perm/struct lkl_ipc_perm/' {} \;
	# Bugfix, lkl_host.h redefines struct iovec in older versions of LKL.
	grep "CONFIG_AUTO_LKL_POSIX_HOST" ${LKL_BUILD}/include/lkl_host.h > /dev/null && find ${LKL_BUILD}/include/ -type f -exec sed -i 's/struct iovec/struct lkl__iovec/' {} \; || true # struct lkl_iovec already exists
	+${MAKE} headers_install -C ${LKL} ARCH=lkl INSTALL_HDR_PATH=${LKL_BUILD}/

tools: ${TOOLS_OBJ}

# Generic tool rule (doesn't actually depend on lkl_lib, but on LKL headers)
${TOOLS_BUILD}/%: ${TOOLS}/%.c ${HOST_MUSL_CC} ${LKL_LIB} | ${TOOLS_BUILD}
	${HOST_MUSL_CC} ${SGXLKL_CFLAGS} --static -I${LKL_BUILD}/include/ -o $@ $<

# More headers required by SGX-Musl not exported by LKL, given by a custom tool's output
${LKL_SGXMUSL_HEADERS}: ${LKL_BUILD}/include/lkl/%.h: ${TOOLS_BUILD}/lkl_%
	$< > $@

# SGX-LKL-Musl
sgx-lkl-musl-config:
	cd ${SGX_LKL_MUSL}; [ -f config.mak ] || CFLAGS="$(MUSL_CFLAGS)" ./configure \
		$(MUSL_CONFIGURE_OPTS) \
		--prefix=${SGX_LKL_MUSL_BUILD} \
		--lklheaderdir=${LKL_BUILD}/include/ \
		--lkllib=${LIBLKL} \
		--sgxlklheaderdir=${MAKE_ROOT}/src/include \
		--sgxlkllib=${BUILD_DIR}/sgxlkl/libsgxlkl.a \
		--disable-shared \
		--enable-sgx-hw=${HW_MODE}

RTE_TARGET ?= build
DPDK_LIBS = -lrte_pmd_vmxnet3_uio -lrte_pmd_ixgbe -lrte_pmd_e1000
DPDK_LIBS += -lrte_pmd_virtio
DPDK_LIBS += -lrte_timer -lrte_hash -lrte_mbuf -lrte_ethdev -lrte_eal
DPDK_LIBS += -lrte_mempool -lrte_ring -lrte_pmd_ring
DPDK_LIBS += -lrte_kvargs -lrte_net  -lrte_bus_vdev -lrte_cmdline
DPDK_LIBS += -lrte_bus_pci -lrte_pci
DPDK_LIBS += -lnuma
LKL_LDFLAGS += -L$(DPDK_BUILD)/lib
LKL_LDFLAGS += -L$(NUMACTL_BUILD)/lib
LKL_LDFLAGS +=-Wl,--whole-archive $(DPDK_LIBS) -Wl,--no-whole-archive -lm -ldl
LDFLAGS="${LKL_LDFLAGS}"

sgx-lkl-musl: ${LIBLKL} ${LKL_SGXMUSL_HEADERS} sgx-lkl-musl-config sgx-lkl $(ENCLAVE_DEBUG_KEY) | ${SGX_LKL_MUSL_BUILD}
	+${MAKE} -C ${SGX_LKL_MUSL} CFLAGS="$(MUSL_CFLAGS) ${LDFLAGS}"
	cp $(SGX_LKL_MUSL)/lib/libsgxlkl.so $(BUILD_DIR)/libsgxlkl.so
# This way the debug info will be automatically picked up when debugging with gdb. TODO: Fix...
	@if [ "$(HW_MODE)" = "yes" ]; then objcopy --only-keep-debug $(BUILD_DIR)/libsgxlkl.so $(BUILD_DIR)/sgx-lkl-run.debug; fi

sgx-lkl-sign: $(BUILD_DIR)/libsgxlkl.so $(ENCLAVE_DEBUG_KEY)
	@if [ "$(HW_MODE)" = "yes" ]; then $(BUILD_DIR)/sgx-lkl-sign -t $(NUM_TCS) -k $(ENCLAVE_DEBUG_KEY) -f $(BUILD_DIR)/libsgxlkl.so; fi

# compile sgx-lkl sources

sgx-lkl: sgx-lkl-musl-config
	make -C src all HW_MODE=$(HW_MODE) LIB_SGX_LKL_BUILD_DIR="$(BUILD_DIR)"

$(ENCLAVE_DEBUG_KEY):
	@mkdir -p $(dir $@ )
	tools/gen_enclave_key.sh $@

enclave-debug-key: $(ENCLAVE_DEBUG_KEY)

# Build directories (one-shot after git clone or clean)
${BUILD_DIR} ${TOOLS_BUILD} ${LKL_BUILD} ${HOST_MUSL_BUILD} ${SGX_LKL_MUSL_BUILD}:
	@mkdir -p $@

# Submodule initialisation (one-shot after git clone)
${HOST_MUSL}/.git ${LKL}/.git ${SGX_LKL_MUSL}/.git ${DPDK}/.git ${NUMACTL}.git:
	[ "$(FORCE_SUBMODULES_VERSION)" = "true" ] || git submodule update --init $($@:.git=)

compdb:
	${MAKE} clean
	bear ${MAKE} DPDK_BEAR_HACK=yes

clean:
	rm -rf ${BUILD_DIR}
	+${MAKE} -C ${HOST_MUSL} distclean || true
	+${MAKE} -C ${SGX_LKL_MUSL} distclean || true
	+${MAKE} -C ${LKL} clean || true
	+${MAKE} -C ${LKL}/tools/lkl clean || true
	+${MAKE} -C src LIB_SGX_LKL_BUILD_DIR="$(BUILD_DIR)" clean || true
	+${MAKE} -C ${NUMACTL} clean || true
	rm -f ${HOST_MUSL}/config.mak
	rm -f ${SGX_LKL_MUSL}/config.mak
	rm -f ${NUMACTL}/{configure,Makefile}
