// This controller module is part of a test in tests/362_controller_dummy
//
// If you change this file you'll likely brake that test.  If you need to
// edit this, make sure that that you also fix tests/362_controller_dummy,
// without changing the nature of that test.


#include <stdio.h>

#include "../../../../../include/quickstream/app.h"
#include "../../../../../include/quickstream/controller.h"
#include "../../../../../lib/debug.h"



void help(FILE *f) {
    fprintf(f,
"   Usage: tests/dummy\n"
"\n"
" A test controller module that does nothing.\n"
"\n"
" This controller module is part of a test in\n"
"tests/362_controller_dummy.\n"
"If you change this file you'll likely brake\n"
"that test.  If you need to edit this, make\n"
"sure that that you also fix\n"
"tests/362_controller_dummy without changing\n"
"the nature of that test.\n"
"\n"
"\n");
}


int construct(int argc, const char **argv) {

    DSPEW("in construct()");
    printf("%s()\n", __func__);

#if 0
    DSPEW("qsControllerUnload(0)");
    qsControllerUnload(0);
#endif

    return 0; // success
}

int preStart(struct QsStream *stream, struct QsFilter *f,
        uint32_t numInputs, uint32_t numOutputs) {

    DSPEW();
    printf("%s()\n", __func__);
    return 1; // done for this stream running.
}

int postStart(struct QsStream *stream, struct QsFilter *f,
        uint32_t numInputs, uint32_t numOutputs) {

    DSPEW();
    printf("%s()\n", __func__);
    return 1; // done for this stream running.
}

int preStop(struct QsStream *stream, struct QsFilter *f,
        uint32_t numInputs, uint32_t numOutputs) {

    DSPEW();
    printf("%s()\n", __func__);
    return 1; // done for this stream running.
}

int postStop(struct QsStream *stream, struct QsFilter *f,
        uint32_t numInputs, uint32_t numOutputs) {

    DSPEW();
    printf("%s()\n", __func__);
    return 1; // done for this stream running.
}

int destroy(void) {

    DSPEW();
    printf("%s()\n", __func__);
    return 0; // success
}
