#include <stdio.h>
#include "sgx_enclave_config.h"

static int check_address(void *addr) {
    void *start = get_enclave_parms()->base;
    void *end = start + get_enclave_parms()->heap_size;
    return addr >= start && addr < end;
}

void lthread_print_backtrace(void) {
    void **frame = __builtin_frame_address(0);
    for (;;) {
        // Ensure that the current frame is safe to access.
        if (!check_address(frame))
            break;

        // Ensure that the return address is valid.
        if (!check_address(frame[1]))
            break;
        printf("%p ", frame[1]);

        // Store address and move to previous frame.
        frame = (void**)*frame;
    }
    puts("\n");
}
