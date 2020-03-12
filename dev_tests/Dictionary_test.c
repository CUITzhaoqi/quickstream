#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>

#include "../lib/Dictionary.h"
#include "../lib/debug.h"


void catchSegv(int sig) {
    fprintf(stderr, "\nCaught signal %d\n"
            "\nsleeping:  gdb -pid %u\n",
            sig, getpid());
    while(true) usleep(100000);
}


int main(void) {

    signal(SIGSEGV, catchSegv);

    struct QsDictionary *d = qsDictionaryCreate();

#if 1
    const char *keys[] = {
        "hello", "hello",
        "0123", "0123",
        "01234", "0123",
        "0123456", "0123",
        "012345", "0123",
        "123", "0123",
        "1111111", "111111",
        "11111", "111111",
        "1111x11", "111111",
        "1111x111", "111111",
        "1111z11", "111111",
        "1111g11", "111111",
        "1111", "111111",
        "x00123", "0123",
        "w00123", "0123",
        "00123", "0123",
        "0", "0",
        "0124", "0123",
        "099", "099",
        "foo", "fooVal",
        "bar", "barVal",
        "boo", "boo",
        "Bar", "Bar",
        "Star start3", "Star:start3Val",
        "Star start", "poo",
        "Star star", "poo",
        "dog", "poo",
        "cat", "poo",
        "00", "0",
        "catdog", "poo   from catdog",
        "hay stack", "needle",
        "h", "h",
        "aa", "aa",
        "ab", "ab",
        "01", "01",
        "02", "02",
        "012", "012",
        "q", "q",

        0, 0
    };
#else
    const char *keys[] = {
        "0123", "0123",
        "01", "01",
        "11", "11",
        "023", "023",
        0, 0
    };
#endif

    for(const char **key = keys; *key; ++key) {
        const char *val = *(key + 1);
        fprintf(stderr, "key=\"%s\", value=\"%s\"\n", *key, val);
        ASSERT(qsDictionaryInsert(d, *key, val) == 0);
        ++key;
    }


    qsDictionaryPrintDot(d, stdout);


    for(const char **key = keys; *key; ++key) {
        const char *stored = qsDictionaryFind(d, *key);
        const char *val = *(key + 1);
        fprintf(stderr, "key=\"%s\", val=\"%s\"\n", *key, val);
        ASSERT(val == stored, "key=\"%s\" val=\"%s\" != stored=\"%s\"",
                *key, val, stored);
        ++key;
    }

    qsDictionaryDestroy(d); 

    return 0;
}
