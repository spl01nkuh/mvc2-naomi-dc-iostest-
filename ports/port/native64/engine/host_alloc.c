/* Optional allocations have the same semantics as upstream's desktop host. */
#include "naomi_host.h"
#include <stdlib.h>

void* host_try_malloc(size_t bytes) { return malloc(bytes); }
void* host_try_calloc(size_t count, size_t bytes) { return calloc(count, bytes); }

void host_out_of_memory(const char* what, size_t bytes) {
    host_log("out of memory: %s (%zu bytes)", what ? what : "native allocation", bytes);
    abort();
}
