#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include "../include/qsapp.h"

//
// This program needs to be linked with a version of libqsapp.so that is
// built with CPP macro DEBUG not set.  Otherwise We'll get DASSERT()
// calls causing the program to stop and wait do to the error.
// To defining DEBUG causes DASSERT() calls, in libqsapp.so code, to
// disappear when it is compiled.
//

static void catcher(int signum) {

    fprintf(stderr, "Caught signal %d\n", signum);
    while(1) usleep(10000);
}


int main(void) {

    signal(SIGSEGV, catcher);

    printf("hello quickstream version %s", QS_VERSION);

    struct QsApp *app = qsAppCreate();

    const int numFilters = 10;
    struct QsFilter *f[numFilters];

    for(int i=0; i<numFilters; ++i)
        if(!(f[i] = qsAppFilterLoad(app, "stdin.so", 0))) {
            qsAppDestroy(app);
            return 1;
        }

    struct QsStream *stream = qsAppStreamCreate(app);
    if(!stream) {
        qsAppDestroy(app);
        return 1;
    }

    // First clear the Qs error to test this:
    free(qsError());
    // First clear the Qs error to test this again:
    free(qsError());

    for(int i=0; i<numFilters-1; ++i)
        if(qsStreamConnectFilters(stream, f[i], f[i+1])) {
            qsAppDestroy(app);
            return 1;
        }


    // This should cause a recoverable error.
    if(qsStreamConnectFilters(stream, f[0], f[0])) {
        char *err = qsError();
        printf("\n\nqsError()=%s\n\n", err);
        free(err);
    }

    printf("DONE\n");


    return 0;
}
