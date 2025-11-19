#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

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

    pthread_t thread, thread2;
    long* three = malloc(sizeof(long));
    *three = 3;
    pthread_create(&thread, NULL, test, three);

    sleep(60);


    *oof = 5;
    pthread_create(&thread2, NULL, test, oof);

    sleep(3);
    return 0;
}
