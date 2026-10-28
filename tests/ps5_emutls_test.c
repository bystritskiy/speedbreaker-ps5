// Emulated TLS must outlive the pthread-key C++ destructor callback.
// Build emutls.c separately with -D__PROSPERO__=1
// -DEMUTLS_USE_POSIX_MEMALIGN=1 -Dfree=ps5_test_free, then link this test
// with -pthread. Run under ASan as well. No PS5 SDK or console is needed.
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct control {
    uintptr_t size, alignment, index;
    void *initial;
};
extern void *__emutls_get_address(struct control *);
static struct control value = {32, 16, 0, NULL};
static void *object;
static atomic_int freed, destroyed, failed;
static pthread_key_t destructor_key;

void ps5_test_free(void *p)
{
    if (p == object)
        atomic_fetch_add(&freed, 1);
    free(p);
}

static void destroy_object(void *p)
{
    // Do not dereference known freed memory: the ordering failure is enough.
    if (atomic_load(&freed)) {
        fprintf(stderr, "TLS storage was freed before the C++ cleanup callback\n");
        atomic_store(&failed, 1);
    } else if (memcmp(p, "still alive", 12) != 0) {
        fprintf(stderr, "TLS object was corrupted before destruction\n");
        atomic_store(&failed, 1);
    }
    atomic_fetch_add(&destroyed, 1);
}

static void *worker(void *unused)
{
    (void)unused;
    object = __emutls_get_address(&value);
    memcpy(object, "still alive", 12);
    // The emutls key is created by the first access; the platform's C++
    // destructor key is created by registering that object's destructor.
    if (pthread_key_create(&destructor_key, destroy_object) != 0 ||
        pthread_setspecific(destructor_key, object) != 0)
        abort();
    return NULL;
}

int main(void)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, worker, NULL) != 0 || pthread_join(thread, NULL) != 0)
        return 2;
    pthread_key_delete(destructor_key);
    if (atomic_load(&failed) || atomic_load(&destroyed) != 1 || atomic_load(&freed) != 1)
        return 1;
    puts("TLS destructor ran before storage cleanup; storage was then freed once");
    return 0;
}
