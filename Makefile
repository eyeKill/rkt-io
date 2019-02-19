include config.mak

.PHONY: host-musl lkl sgx-lkl-musl-config sgx-lkl-musl sgx-lkl tools clean enclave-debug-key \
	dpdk compdb

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
	grep fno-omit-frame-pointer ${HOST_MUSL_BUILD}/lib/musl-gcc.specs || \
		sed -i -e 's!-nostdinc!-nostdinc -fno-omit-frame-pointer!' ${HOST_MUSL_BUILD}/lib/musl-gcc.specs

define dpdk_build
dpdk-config-$(1) ${DPDK_CONFIG}: ${CURDIR}/src/dpdk/override/defconfig
	make -j1 -C ${DPDK} RTE_SDK=${DPDK} T=${RTE_TARGET} O=${DPDK_BUILD} config
	cat ${DPDK_BUILD}/.config.orig ${CURDIR}/src/dpdk/override/defconfig > ${DPDK_BUILD}/.config
	if [[ "$(DEBUG)" = "true" ]]; then echo 'CONFIG_RTE_LOG_DP_LEVEL=RTE_LOG_DEBUG' >> ${DPDK_BUILD}/.config; fi

# WARNING we currently disable thread local storage (-D__thread=) since there is no support
# for it when running lkl. In particular this affects rte_errno and makes it thread-unsafe.
dpdk-$(1) ${DPDK_BUILD}/lib/libdpdk.a: ${DPDK_CONFIG} | ${DPDK_CC} ${DPDK}/.git ${RTE_SDK}
	+make -j`tools/ncore.sh` -C ${DPDK_BUILD} WERROR_FLAGS= CC=${DPDK_CC} RTE_SDK=${DPDK} V=1 \
		EXTRA_CFLAGS="-Wno-error -lc ${DPDK_EXTRA_CFLAGS} -UDEBUG" \
		|| test ${DPDK_BEAR_HACK} == "yes"
endef

RTE_TARGET = x86_64-native-linuxapp-gcc
# when compiling with BEAR the build seems to fail at some point also the overall build is still fine
DPDK_BEAR_HACK ?= no

DPDK_BUILD := ${DPDK_BUILD_NATIVE}
DPDK_CONFIG = ${DPDK_BUILD}/.config
DPDK_CC := ${CC}
# Since we are only using the native DPDK for initializing, we can always compile it unoptimized
DPDK_EXTRA_CFLAGS := -g -O0
# pseudo target for default CC so we use add a dependency on our musl compiler in dpdk_build
$(CC):
	:
$(eval $(call dpdk_build,native))

DPDK_BUILD := ${DPDK_BUILD_SGX}
DPDK_CONFIG = ${DPDK_BUILD}/.config
DPDK_CC := ${HOST_MUSL_CC}
DPDK_EXTRA_CFLAGS := ${MUSL_CFLAGS} -include ${CURDIR}/src/dpdk/override/uint.h
$(eval $(call dpdk_build,sgx))

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
lkl ${LIBLKL}: ${DPDK_BUILD_SGX}/lib/libdpdk.a ${HOST_MUSL_CC} | ${LKL}/.git ${LKL_BUILD} src/lkl/override/defconfig
	# Override lkl's defconfig with our own
	cp -Rv src/lkl/override/defconfig ${LKL}/arch/lkl/defconfig
	cp -Rv src/lkl/override/include/uapi/asm-generic/stat.h ${LKL}/include/uapi/asm-generic/stat.h
	# Set bootmem size (default in LKL is 64MB)
	sed -i 's/static unsigned long mem_size = .*;/static unsigned long mem_size = ${BOOT_MEM} \* 1024 \* 1024;/g' lkl/arch/lkl/kernel/setup.c
	# Disable loading of kernel symbols for debugging/panics
	grep -q -F 'CONFIG_KALLSYMS=n' ${LKL}/arch/lkl/defconfig || echo 'CONFIG_KALLSYMS=n' >> ${LKL}/arch/lkl/defconfig
	+DESTDIR=${LKL_BUILD} ${MAKE} V=1 -C ${LKL}/tools/lkl -j`tools/ncore.sh` CC=${HOST_MUSL_CC} PREFIX="" \
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

DPDK_LIBS = -Wl,--whole-archive
DPDK_LIBS += -lrte_pmd_i40e
DPDK_LIBS += -lrte_hash -lrte_mbuf -lrte_ethdev -lrte_eal
DPDK_LIBS += -lrte_mempool_ring -lrte_mempool -lrte_ring
DPDK_LIBS += -lrte_kvargs -lrte_net -lrte_cmdline
DPDK_LIBS += -lrte_bus_pci -lrte_pci
DPDK_LIBS += -Wl,--no-whole-archive
DPDK_COMMON_CFLAGS = -msse4.1
# -nostdinc does vanish both libc headers and gcc intriniscs,
# we only want get rid-off libc headers
GCC_HEADERS = $(shell CPP='$(CPP)' ./tools/find-gcc-headers.sh)
DPDK_SGX_CFLAGS = "${DPDK_COMMON_CFLAGS} -I${DPDK_BUILD_SGX}/include -I${GCC_HEADERS}"
DPDK_SGX_LDFLAGS = "-L$(DPDK_BUILD_SGX)/lib $(DPDK_LIBS) ${BUILD_DIR}/dpdk_init_array.o"
DPDK_NATIVE_CFLAGS = "${DPDK_COMMON_CFLAGS} -I${DPDK_BUILD_NATIVE}/include"
DPDK_NATIVE_LDFLAGS = "-L$(DPDK_BUILD_NATIVE)/lib $(DPDK_LIBS)"

${BUILD_DIR}/dpdk_init_array.c: ${DPDK_BUILD_SGX}/lib/libdpdk.a
	./tools/gen_dpdk_init_array.py $@ tools/sgx-lkl.ld ${DPDK_NATIVE_LDFLAGS}

${BUILD_DIR}/dpdk_init_array.o: ${BUILD_DIR}/dpdk_init_array.c
	${HOST_MUSL_CC} -fPIC -c -o $@ $<

sgx-lkl-musl: ${LIBLKL} ${LKL_SGXMUSL_HEADERS} sgx-lkl-musl-config sgx-lkl $(ENCLAVE_DEBUG_KEY) ${BUILD_DIR}/dpdk_init_array.o ${DPDK_BUILD_SGX}/lib/libdpdk.a | ${SGX_LKL_MUSL_BUILD}
	+${MAKE} -C ${SGX_LKL_MUSL} CFLAGS="$(MUSL_CFLAGS)" \
    DPDK_SGX_CFLAGS=$(DPDK_SGX_CFLAGS) \
    DPDK_SGX_LDFLAGS=$(DPDK_SGX_LDFLAGS)

	cp $(SGX_LKL_MUSL)/lib/libsgxlkl.so $(BUILD_DIR)/libsgxlkl.so
# This way the debug info will be automatically picked up when debugging with gdb. TODO: Fix...
	@if [ "$(HW_MODE)" = "yes" ]; then objcopy --only-keep-debug $(BUILD_DIR)/libsgxlkl.so $(BUILD_DIR)/sgx-lkl-run.debug; fi

sgx-lkl-sign: $(BUILD_DIR)/libsgxlkl.so $(ENCLAVE_DEBUG_KEY)
	@if [ "$(HW_MODE)" = "yes" ]; then $(BUILD_DIR)/sgx-lkl-sign -t $(NUM_TCS) -k $(ENCLAVE_DEBUG_KEY) -f $(BUILD_DIR)/libsgxlkl.so; fi

# compile sgx-lkl sources

sgx-lkl: sgx-lkl-musl-config ${DPDK_BUILD_NATIVE}/lib/libdpdk.a ${DPDK_BUILD_SGX}/lib/libdpdk.a
	make -C src all HW_MODE=$(HW_MODE) LIB_SGX_LKL_BUILD_DIR="$(BUILD_DIR)" \
    DPDK_SGX_CFLAGS=$(DPDK_SGX_CFLAGS) \
    DPDK_SGX_LDFLAGS=$(DPDK_SGX_LDFLAGS) \
    DPDK_NATIVE_CFLAGS=${DPDK_NATIVE_CFLAGS} \
    DPDK_NATIVE_LDFLAGS=${DPDK_NATIVE_LDFLAGS}

$(ENCLAVE_DEBUG_KEY):
	@mkdir -p $(dir $@ )
	tools/gen_enclave_key.sh $@

enclave-debug-key: $(ENCLAVE_DEBUG_KEY)

# Build directories (one-shot after git clone or clean)
${BUILD_DIR} ${TOOLS_BUILD} ${LKL_BUILD} ${HOST_MUSL_BUILD} ${SGX_LKL_MUSL_BUILD}:
	@mkdir -p $@

# Submodule initialisation (one-shot after git clone)
${HOST_MUSL}/.git ${LKL}/.git ${SGX_LKL_MUSL}/.git ${DPDK}/.git:
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
	rm -f ${HOST_MUSL}/config.mak
	rm -f ${SGX_LKL_MUSL}/config.mak
