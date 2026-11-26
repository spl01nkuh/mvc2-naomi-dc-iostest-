/* Full-engine link and API lifecycle failures, without commercial ROM data. */
#include "runtime.h"
#include "engine_state.h"
#include "rd.h"
#include <assert.h>
#include <pthread.h>
#include <string.h>

static void* wrong_thread(void* context) {
    (void)context;
    char error[256];
    assert(!mvc2_gameplay_start("/missing-rom", "/tmp", NULL, error, sizeof error));
    assert(strstr(error, "owning thread"));
    assert(!mvc2_gameplay_frame());
    assert(!mvc2_gameplay_frame_width() && !mvc2_gameplay_frame_height());
    mvc2_gameplay_stop();
    return NULL;
}

int main(void) {
    char error[1024];
    const int16_t* samples = (const int16_t*)1;
    assert(mvc2_gameplay_progress().task == -1);
    assert(!mvc2_gameplay_frame());
    assert(!mvc2_gameplay_frame_width() && !mvc2_gameplay_frame_height());
    assert(!mvc2_gameplay_audio(&samples) && !samples);
    assert(!mvc2_gameplay_start(NULL, NULL, NULL, error, sizeof error));
    assert(error[0]);
    assert(!mvc2_gameplay_start("/missing-rom", "/tmp", NULL, error, sizeof error));
    assert(error[0]);
    assert(!mvc2_gameplay_step(NULL, error, sizeof error));
    assert(strstr(error, "not been started"));
#if defined(__clang__) && (defined(__APPLE__) || defined(__linux__))
    // Test the actual linked game/backend spans, as well as the smaller
    // checkpoint fixture. Host ownership state remains outside those spans.
    assert(mvc2_engine_checkpoint());
    rd_set_widescreen(1);
    assert(rd_width() == 854);
    mvc2_engine_restore();
    assert(rd_width() == 640);
#endif
    mvc2_gameplay_stop();
    mvc2_gameplay_stop();
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, wrong_thread, NULL));
    assert(!pthread_join(thread, NULL));
    return 0;
}
