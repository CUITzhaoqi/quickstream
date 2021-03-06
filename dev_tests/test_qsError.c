#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include "../include/quickstream/app.h"

//
// This program needs to be linked with a version of libquickstream.so
// that is built with CPP macro DEBUG not set.  Otherwise We'll get
// DASSERT() calls causing the program to stop and wait do to the error.
// To defining DEBUG causes DASSERT() calls, in libquickstream.so code,
// to disappear when it is compiled.
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
    struct QsStream *stream = qsAppStreamCreate(app);
    if(!stream) {
        qsAppDestroy(app);
        return 1;
    }


    for(int i=0; i<numFilters; ++i)
        if(!(f[i] = qsStreamFilterLoad(stream, "stdin.so", 0, 0, 0))) {
            qsAppDestroy(app);
            return 1;
        }

    // First clear the Qs error to test this again:
    qsError();

    for(int i=0; i<numFilters-1; ++i)
        qsFiltersConnect(f[i], f[i+1], 0, 0);

   // Clear the Qs error to test this again:
    qsError();

    // This should cause a recoverable error.
    qsFiltersConnect(f[0], f[0], 0, 0);

    printf("DONE\n");


    return 0;
}
