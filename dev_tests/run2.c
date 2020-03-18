#include <unistd.h>
#include <signal.h>

#include "../include/quickstream/app.h"


// Turn on spew macros for this file.
#ifndef SPEW_LEVEL_DEBUG
#  define SPEW_LEVEL_DEBUG
#endif
#include "../lib/debug.h"


static void catcher(int signum) {

    WARN("Caught signal %d\n", signum);
    fprintf(stderr, "Try running:\n\n"
            "    gdb -pid %u\n\n", getpid());
    while(1) usleep(10000);
}


int main(void) {

    signal(SIGSEGV, catcher);

    INFO("hello quickstream version %s", QS_VERSION);

    struct QsApp *app = qsAppCreate();
    struct QsStream *stream = qsAppStreamCreate(app);

    struct QsFilter *f0 = qsStreamFilterLoad(stream, "stdin", 0, 0, 0);
    struct QsFilter *f1 = qsStreamFilterLoad(stream, "tests/sleep", 0, 0, 0);
    struct QsFilter *f2 = qsStreamFilterLoad(stream, "stdout.so", 0, 0, 0);
    qsStreamConnectFilters(stream, f0, f1, 0, 0);
    qsStreamConnectFilters(stream, f1, f2, 0, 0);


    qsStreamReady(stream);

    qsAppDisplayFlowImage(app, 3, false);

    qsStreamLaunch(stream, 1/*maxThreads*/);

    qsStreamStop(stream);

    qsAppDestroy(app);

    WARN("SUCCESS");

    return 0;
}
