#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>

void* test(void* arg) {
    printf("OFFSHOOT TEST: %ld\n", *((long*)arg));
    free(arg);
    //printf("POSTFREE\n");
    return NULL;
}

int main(void) {
    int* oof = calloc(4,1);
    if (oof) printf("SUCCESS!\n");
    else printf("FAILURE!\n");

    int pid = fork();

    char* args[] = {"./hi", NULL};
    if (pid == 0) execv("./hi", args);

    pthread_t thread, thread2;
    long* three = malloc(sizeof(long));
    *three = 3;
    pthread_create(&thread, NULL, test, three);

    sleep(5);

    void* map_test = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANON | MAP_NONBLOCK, -1, 0);

    *oof = 5;
    pthread_create(&thread2, NULL, test, oof);

    sleep(3);

    munmap(map_test, 4096);
    return 0;
}
