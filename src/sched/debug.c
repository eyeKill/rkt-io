#include <stdio.h>
#include "linux/types.h"
#include "sgx_enclave_config.h"

#ifdef SGXLKL_HW
static int check_address(uintptr_t addr) {
    uintptr_t start = get_enclave_parms()->base;
    uintptr_t end = start + get_enclave_parms()->heap_size;
    return addr >= start && addr < end;
}

// copy and paste example to persist the backtrace.
//#define NR_CPUS 4
//void* bt_per_cpu[NR_CPUS][4][10];
//static void save_backtrace(void **frame, void *bt_per_cpu[10]) {
//    size_t i;
//    for (i = 0; i < sizeof(*bt_per_cpu); i++) {
//        // Ensure that the current frame is safe to access.
//        if (!check_address((uintptr_t)frame))
//            break;
//
//        // Ensure that the return address is valid.
//        if (!check_address((uintptr_t)frame[1]))
//            break;
//
//        bt_per_cpu[i] = frame[1];
//        if (frame == *frame) {
//            break;
//        }
//
//        // Store address and move to previous frame.
//        frame = (void**)*frame;
//
//        if ((uintptr_t)frame < 0x1000) {
//            break;
//        }
//    }
//}

void lthread_print_backtrace(void) {
    void **frame = __builtin_frame_address(0);
    for (;;) {
        // Ensure that the current frame is safe to access.
        if (!frame || !check_address((uintptr_t)frame))
            break;

        // Ensure that the return address is valid.
        if (!frame[1] || !check_address((uintptr_t)frame[1]))
            break;
        printf("%p ", frame[1]);
        if (frame == *frame) {
            break;
        }

        // Store address and move to previous frame.
        frame = (void**)*frame;

        // sgx-lkl also maps the first page, which cannot be accessed.
        if ((uintptr_t)frame < 0x1000) {
            break;
        }
    }
    puts("\n");
}
#else
void lthread_print_backtrace(void) {}
#endif
