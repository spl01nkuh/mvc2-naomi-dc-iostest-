/* Full-engine link and API lifecycle failures, without commercial ROM data. */
#include "runtime.h"
#include <assert.h>
#include <pthread.h>
#include <string.h>

static void* wrong_thread(void* context) {
    (void)context;
    char error[256];
    assert(!mvc2_gameplay_start("/missing-rom", "/tmp", error, sizeof error));
    assert(strstr(error, "owning thread"));
    assert(!mvc2_gameplay_frame());
    mvc2_gameplay_stop();
    return NULL;
}

int main(void) {
    char error[1024];
    const int16_t* samples = (const int16_t*)1;
    assert(!mvc2_gameplay_frame());
    assert(!mvc2_gameplay_audio(&samples) && !samples);
    assert(!mvc2_gameplay_start(NULL, NULL, error, sizeof error));
    assert(error[0]);
    assert(!mvc2_gameplay_start("/missing-rom", "/tmp", error, sizeof error));
    assert(error[0]);
    assert(!mvc2_gameplay_step(NULL, error, sizeof error));
    assert(strstr(error, "not been started"));
    mvc2_gameplay_stop();
    mvc2_gameplay_stop();
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, wrong_thread, NULL));
    assert(!pthread_join(thread, NULL));
    return 0;
}
