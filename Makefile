include config.mak

.PHONY: host-musl lkl sgx-lkl-musl-config sgx-lkl-musl sgx-lkl tools clean enclave-debug-key \
	dpdk compdb

PREFIX=/usr/local

# Boot memory reserved for LKL/kernel (in MB)
BOOT_MEM=32 # Default in LKL is 64

# Max. number of enclave threads/TCS
NUM_TCS=8

HW_MODE=yes

default: all

# Default is to build everything
all: sgx-lkl-musl sgx-lkl gdb/sgx-lkl-gdb

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
	ln -fs ${LINUX_HEADERS_INC}/x86_64-linux-gnu/asm/ ${HOST_MUSL_BUILD}/include/asm
	ln -fs ${LINUX_HEADERS_INC}/asm-generic/ ${HOST_MUSL_BUILD}/include/asm-generic
	install third_party/sys-queue.h ${HOST_MUSL_BUILD}/include/sys/queue.h
	# Fix musl-gcc for gcc version that have been built with --enable-default-pie
	gcc -v 2>&1 | grep "\-\-enable-default-pie" > /dev/null && sed -i 's/"$$@"/-fpie -pie "\$$@"/g' ${HOST_MUSL_BUILD}/bin/musl-gcc || true
	grep fno-omit-frame-pointer ${HOST_MUSL_BUILD}/lib/musl-gcc.specs || \
		sed -i -e 's!-nostdinc!-nostdinc -fno-omit-frame-pointer!' ${HOST_MUSL_BUILD}/lib/musl-gcc.specs

define dpdk_build
dpdk-config-${1} ${DPDK_CONFIG}: ${CURDIR}/src/dpdk/override/defconfig | ${DPDK}/.git
	make -j1 -C ${DPDK} RTE_SDK=${DPDK} T=x86_64-native-linuxapp-gcc O=${DPDK_BUILD} config
	cat ${DPDK_BUILD}/.config.orig ${CURDIR}/src/dpdk/override/defconfig > ${DPDK_BUILD}/.config
	if [[ "$(DEBUG)" = "true" ]]; then echo 'CONFIG_RTE_LOG_DP_LEVEL=RTE_LOG_DEBUG' >> ${DPDK_BUILD}/.config; fi

# WARNING we currently disable thread local storage (-D__thread=) since there is no support
# for it when running lkl. In particular this affects rte_errno and makes it thread-unsafe.
dpdk-${1} ${DPDK_BUILD}/.build: ${DPDK_CONFIG} | ${DPDK_CC} ${DPDK}/.git
	+make -j`tools/ncore.sh` -C ${DPDK_BUILD} WERROR_FLAGS= CC=${DPDK_CC} RTE_SDK=${DPDK} V=1 \
		EXTRA_CFLAGS="-Wno-error -lc ${DPDK_EXTRA_CFLAGS} -UDEBUG" \
		|| test ${DPDK_BEAR_HACK} == "yes"
	touch ${DPDK_BUILD}/.build

spdk-source-${1} ${SPDK_BUILD}/mk: | ${SPDK}/.git
	rsync -a ${CURDIR}/spdk/ ${SPDK_BUILD}/

spdk-config-${1} ${SPDK_CONFIG}: ${CURDIR}/src/spdk/override/config.mk | ${SPDK_BUILD}/mk
	cp ${CURDIR}/src/spdk/override/config.mk ${SPDK_CONFIG}
	echo 'CONFIG_DPDK_DIR=$(DPDK_BUILD)' >> ${SPDK_CONFIG}

spdk-cflags-${1} ${SPDK_BUILD}/mk/cc.flags.mk: | ${DPDK_CC} ${SPDK_BUILD}/mk
	echo CFLAGS="-I${DPDK_BUILD}/include -I${LIBUUID_HOST_BUILD}/include -msse4.2" > ${SPDK_BUILD}/mk/cc.flags.mk
	echo LDFLAGS="-L${DPDK_BUILD}/lib" >> ${SPDK_BUILD}/mk/cc.flags.mk

spdk-${1} ${SPDK_BUILD}/.build: ${DPDK_BUILD}/.build spdk-source-$(1) ${SPDK_CONFIG} ${SPDK_BUILD}/mk/cc.flags.mk | ${DPDK_CC}
	make -C ${SPDK_BUILD} CC=${DPDK_CC}
	touch ${SPDK_BUILD}/.build
endef

# when compiling with BEAR the build seems to fail at some point also the overall build is still fine
DPDK_BEAR_HACK ?= no

SPDK_BUILD := ${SPDK_BUILD_NATIVE}
DPDK_BUILD := ${DPDK_BUILD_NATIVE}
DPDK_CONFIG = ${DPDK_BUILD}/.config
SPDK_CONFIG = ${SPDK_BUILD}/mk/config.mk
DPDK_CC := ${CC}
# Since we are only using the native DPDK for initializing, we can always compile it unoptimized
DPDK_EXTRA_CFLAGS := -g -O0
# pseudo target for default CC so we use add a dependency on our musl compiler in dpdk_build
$(CC):
	:
$(eval $(call dpdk_build,native))

SPDK_BUILD := ${SPDK_BUILD_SGX}
DPDK_BUILD := ${DPDK_BUILD_SGX}
DPDK_CONFIG = ${DPDK_BUILD}/.config
SPDK_CONFIG = ${SPDK_BUILD}/mk/config.mk
DPDK_CC := ${HOST_MUSL_CC}
DPDK_EXTRA_CFLAGS := ${MUSL_CFLAGS} -include ${CURDIR}/src/dpdk/override/uint.h
$(eval $(call dpdk_build,sgx))

undefine SPDK_BUILD
undefine DPDK_BUILD
undefine DPDK_CONFIG
undefine DPDK_CC
undefine DPDK_EXTRA_CFLAGS

# Since load-dpdk-driver may require root,
# we don't want to build the kernel modules here and
# ask the user to run make instead
${DPDK_BUILD_NATIVE}/kmod/%.ko:
	$(error "Kernel module $@ not found, run `make dpdk` first")

load-dpdk-driver: ${DPDK_BUILD_NATIVE}/kmod/igb_uio.ko ${DPDK_BUILD_NATIVE}/kmod/rte_kni.ko
	modprobe uio || true
	rmmod rte_kni || true
	rmmod igb_uio || true
	insmod ${DPDK_BUILD_NATIVE}/kmod/rte_kni.ko
	insmod ${DPDK_BUILD_NATIVE}/kmod/igb_uio.ko

# LKL's static library and include/ header directory
lkl ${LIBLKL} ${LKL_BUILD}/include: ${DPDK_BUILD_SGX}/.build ${HOST_MUSL_CC} | ${LKL}/.git ${LKL_BUILD} src/lkl/override/defconfig
	# Override lkl's defconfig with our own
	cp -Rv src/lkl/override/defconfig ${LKL}/arch/lkl/defconfig
	cp -Rv src/lkl/override/include/uapi/asm-generic/stat.h ${LKL}/include/uapi/asm-generic/stat.h
	grep "include \"sys/stat.h" lkl/tools/lkl/include/lkl.h > /dev/null || sed  -i '/define _LKL_H/a \\n#include "sys/stat.h"\n#include "time.h"' lkl/tools/lkl/include/lkl.h
	# Set bootmem size (default in LKL is 64MB)
	sed -i 's/static unsigned long mem_size = .*;/static unsigned long mem_size = ${BOOT_MEM} \* 1024 \* 1024;/g' lkl/arch/lkl/kernel/setup.c
	+DESTDIR=${LKL_BUILD} ${MAKE} -C ${LKL}/tools/lkl -j`tools/ncore.sh` CC=${HOST_MUSL_CC} PREFIX="" \
		${LKL}/tools/lkl/liblkl.a
	mkdir -p ${LKL_BUILD}/lib
	# they don't seem to recompile anything unless I touch this:
	rm ${LKL}/tools/lkl/Makefile.conf
	cp ${LKL}/tools/lkl/liblkl.a $(LKL_BUILD)/lib
	+DESTDIR=${LKL_BUILD} ${MAKE} -C ${LKL}/tools/lkl -j`tools/ncore.sh` CC=${HOST_MUSL_CC} PREFIX="" \
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

${CRYPTSETUP_BUILD}/lib/libcryptsetup.a ${CRYPTSETUP_BUILD}/lib/libpopt.a ${CRYPTSETUP_BUILD}/lib/libdevmapper.a ${CRYPTSETUP_BUILD}/lib/libuuid.a ${CRYPTSETUP_BUILD}/lib/libjson-c.a ${MBEDTLS}/mbedtls.a ${LIBUUID_HOST_BUILD}/lib/libuuid.a: ${LKL_BUILD}/include
	+${MAKE} -C ${MAKE_ROOT}/third_party $@

# More headers required by SGX-Musl not exported by LKL, given by a custom tool's output
${LKL_SGXMUSL_HEADERS}: ${LKL_BUILD}/include/lkl/%.h: ${TOOLS_BUILD}/lkl_%
	$< > $@

${BUILD_DIR}/init_array.c: ${SPDK_BUILD_SGX}/.build
	./tools/gen_init_array.py $@ tools/sgx-lkl.ld "${SPDK_NATIVE_LDFLAGS}"

+${BUILD_DIR}/init_array.o: ${BUILD_DIR}/init_array.c
	${HOST_MUSL_CC} -fPIC -c -o $@ $<

# SGX-LKL-Musl
sgx-lkl-musl-config:
	cd ${SGX_LKL_MUSL}; [ -f config.mak ] || CFLAGS="$(MUSL_CFLAGS)" ./configure \
		$(MUSL_CONFIGURE_OPTS) \
		--prefix=${SGX_LKL_MUSL_BUILD} \
		--lklheaderdir=${LKL_BUILD}/include/ \
		--lkllib=${LIBLKL} \
		--sgxlklheaderdir=${MAKE_ROOT}/src/include \
		--sgxlkllib=${BUILD_DIR}/sgxlkl/libsgxlkl.a \
		--cryptsetupheaderdir=${CRYPTSETUP_BUILD}/include/ \
		--cryptsetuplib="${CRYPTSETUP_BUILD}/lib/libcryptsetup.a ${CRYPTSETUP_BUILD}/lib/libpopt.a ${CRYPTSETUP_BUILD}/lib/libdevmapper.a ${CRYPTSETUP_BUILD}/lib/libuuid.a ${CRYPTSETUP_BUILD}/lib/libjson-c.a" \
		--disable-shared \
		--enable-sgx-hw=${HW_MODE}

SPDK_LIBS = -Wl,--whole-archive
SPDK_LIBS += -lspdk_nvme -lspdk_sock -lspdk_bdev
SPDK_LIBS += -lspdk_thread -lspdk_trace -lspdk_conf -lspdk_json
SPDK_LIBS += -lspdk_env_dpdk -lspdk_util -lspdk_log
# DPDK
SPDK_LIBS += -lrte_pmd_i40e
SPDK_LIBS += -lrte_hash -lrte_mbuf -lrte_ethdev -lrte_eal
SPDK_LIBS += -lrte_mempool_ring -lrte_mempool -lrte_ring
SPDK_LIBS += -lrte_kvargs -lrte_net -lrte_cmdline
SPDK_LIBS += -lrte_bus_pci -lrte_pci
SPDK_LIBS += -Wl,--no-whole-archive
# -nostdinc does vanish both libc headers and gcc intriniscs,
# we only want get rid-off libc headers
GCC_HEADERS = $(shell CPP='${CPP}' ./tools/find-gcc-headers.sh)
SPDK_SGX_CFLAGS = -msse4.2 -I${DPDK_BUILD_SGX}/include -I${SPDK_BUILD_SGX}/include -I${GCC_HEADERS}
SPDK_SGX_LDFLAGS = -L${DPDK_BUILD_SGX}/lib -L${SPDK_BUILD_SGX}/build/lib ${SPDK_LIBS}
SPDK_SGX_LDFLAGS += ${BUILD_DIR}/init_array.o

SPDK_NATIVE_CFLAGS = -msse4.2 -I${DPDK_BUILD_NATIVE}/include -I${SPDK_BUILD_NATIVE}/include
SPDK_NATIVE_LDFLAGS = -L${DPDK_BUILD_NATIVE}/lib -L${SPDK_BUILD_NATIVE}/build/lib -L${LIBUUID_HOST_BUILD}/lib ${SPDK_LIBS}
SPDK_NATIVE_LDFLAGS += -luuid

sgx-lkl-musl: ${LIBLKL} ${LKL_SGXMUSL_HEADERS} ${CRYPTSETUP_BUILD}/lib/libcryptsetup.a sgx-lkl-musl-config sgx-lkl $(ENCLAVE_DEBUG_KEY) ${BUILD_DIR}/init_array.o ${SPDK_BUILD_SGX}/.build | ${SGX_LKL_MUSL_BUILD}
	+${MAKE} -C ${SGX_LKL_MUSL} CFLAGS="$(MUSL_CFLAGS)" \
    SPDK_SGX_CFLAGS="$(SPDK_SGX_CFLAGS)" \
    SPDK_SGX_LDFLAGS="$(SPDK_SGX_LDFLAGS)"
	cp $(SGX_LKL_MUSL)/lib/libsgxlkl.so $(BUILD_DIR)/libsgxlkl.so
# This way the debug info will be automatically picked up when debugging with gdb. TODO: Fix...
	@if [ "$(HW_MODE)" = "yes" ]; then objcopy --only-keep-debug $(BUILD_DIR)/libsgxlkl.so $(BUILD_DIR)/sgx-lkl-run.debug; fi

sgx-lkl-sign: $(BUILD_DIR)/libsgxlkl.so $(ENCLAVE_DEBUG_KEY)
	@if [ "$(HW_MODE)" = "yes" ]; then $(BUILD_DIR)/sgx-lkl-sign -t $(NUM_TCS) -k $(ENCLAVE_DEBUG_KEY) -f $(BUILD_DIR)/libsgxlkl.so; fi

# compile sgx-lkl sources

sgx-lkl: sgx-lkl-musl-config ${MBEDTLS}/mbedtls.a ${LIBUUID_HOST_BUILD}/lib/libuuid.a ${SPDK_BUILD_NATIVE}/.build ${SPDK_BUILD_SGX}/.build
	make -C src all HW_MODE=$(HW_MODE) LIB_SGX_LKL_BUILD_DIR="$(BUILD_DIR)" \
    SPDK_SGX_CFLAGS="$(SPDK_SGX_CFLAGS)" \
    SPDK_SGX_LDFLAGS="$(SPDK_SGX_LDFLAGS)" \
    SPDK_NATIVE_CFLAGS="${SPDK_NATIVE_CFLAGS}" \
    SPDK_NATIVE_LDFLAGS="${SPDK_NATIVE_LDFLAGS}"

gdb/sgx-lkl-gdb:
	cd gdb && ./setup.sh

$(ENCLAVE_DEBUG_KEY):
	@mkdir -p $(dir $@ )
	tools/gen_enclave_key.sh $@

enclave-debug-key: $(ENCLAVE_DEBUG_KEY)

# Build directories (one-shot after git clone or clean)
${BUILD_DIR} ${TOOLS_BUILD} ${LKL_BUILD} ${HOST_MUSL_BUILD} ${SGX_LKL_MUSL_BUILD} ${CRYPTSETUP_BUILD}:
	@mkdir -p $@

# Submodule initialisation (one-shot after git clone)
${HOST_MUSL}/.git ${LKL}/.git ${SGX_LKL_MUSL}/.git ${DPDK}/.git ${SPDK}/.git:
	[ "$(FORCE_SUBMODULES_VERSION)" = "true" ] || git submodule update --init $($@:.git=)

compdb:
	${MAKE} clean
	bear ${MAKE} DPDK_BEAR_HACK=yes

install: $(BUILD_DIR)/libsgxlkl.so $(BUILD_DIR)/sgx-lkl-run
	mkdir -p ${PREFIX}/bin ${PREFIX}/lib
	cp $(BUILD_DIR)/libsgxlkl.so $(PREFIX)/lib
	cp $(BUILD_DIR)/sgx-lkl-run $(PREFIX)/bin
	cp $(BUILD_DIR)/sgx-lkl-sign $(PREFIX)/bin
	cp $(TOOLS)/sgx-lkl-java $(PREFIX)/bin
	cp $(TOOLS)/sgx-lkl-disk $(PREFIX)/bin

uninstall:
	rm -rf ~/.cache/sgxlkl
	rm -f $(PREFIX)/lib/libsgxlkl.so
	rm -f $(PREFIX)/bin/sgx-lkl-run
	rm -f $(PREFIX)/bin/sgx-lkl-sign
	rm -f $(PREFIX)/bin/sgx-lkl-java
	rm -f $(PREFIX)/bin/sgx-lkl-disk

clean:
	rm -rf ${BUILD_DIR}
	+${MAKE} -C ${HOST_MUSL} distclean || true
	+${MAKE} -C ${SGX_LKL_MUSL} distclean || true
	+${MAKE} -C ${LKL} clean || true
	+${MAKE} -C ${LKL}/tools/lkl clean || true
	+${MAKE} -C ${MAKE_ROOT}/third_party clean || true
	+${MAKE} -C src LIB_SGX_LKL_BUILD_DIR="$(BUILD_DIR)" clean || true
	rm -f ${HOST_MUSL}/config.mak
	rm -f ${SGX_LKL_MUSL}/config.mak
