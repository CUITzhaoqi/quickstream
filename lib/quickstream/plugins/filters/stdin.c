#include "../../../../include/quickstream/filter.h"
#include "../../../../lib/debug.h"

void help(FILE *f) {

    fprintf(f,
        "This filter is usually a source.\n"
        "This filter will ignore all inputs.\n"
        "This filter will read stdin and write it to all outputs.\n"
        "\n"
// TODO:
        "TODO: add a stdin fread() buffer size option.  Without it when there\n"
        "is relatively slow input data this may block on the fread() call to\n"
        "to make the stream function properly.\n"
        "\n");
}


static const size_t OutLen = QS_DEFAULTMAXWRITE;


int input(void *buffers[], const size_t lens[],
        const bool isFlushing[],
        uint32_t numInputs, uint32_t numOutputs) {

    // We'll assume there is no input data.
    ASSERT(numInputs == 0);
    ASSERT(numOutputs == 1);

    // For output buffering from this filter.
    void *buffer = qsGetOutputBuffer(0, OutLen, 0);

    // Put data in the output buffer.
    size_t rd = fread(buffer, 1, OutLen, stdin);
    if(rd)
        qsOutput(0, rd);

    // handle the stream closing and end of file.
    if(feof(stdin))
        // This filter is done reading stdin.
        return 1; // filter done.

    return 0; // continue.
}
