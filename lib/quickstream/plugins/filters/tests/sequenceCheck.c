#include "../../../../../include/quickstream/filter.h"
#include "../../../../../lib/debug.h"

#include "Sequence.h"

#define DEFAULT_SEEDOFFSET ((uint32_t) 0)

void help(FILE *f) {

    fprintf(f,
        "The filter reads and checks input that came from sequenceGen.\n"
        "The filter reads a number of inputs and writes out what is reads\n"
        "in.   If there are fewer inputs than outputs, than the last input\n"
        "is written to the remainder outputs.  If there are more inputs than\n"
        "outputs, than the later inputs are only checked and not written out.\n"
        "\n"
        "\n"
        "                  OPTIONS\n"
        "\n"
        "\n"
        "   --maxWrite BYTES  default value %zu.  This is the number of\n"
        "                     bytes read and written for each input() call.\n"
        "\n"
        "   --seedStart NUM  Starting seed number is NUM and increases by\n"
        "                    by one for each input port.  The default NUM\n"
        "                    is %" PRIu32 ".\n"
        "\n"
        "   --seeds \"N0 N1 N2 ...\"  The seed number for each input port.\n"
        "                             This will override the seed value from\n"
        "                             a --seedStart option.\n"
        "\n"
        "\n",
        QS_DEFAULTMAXWRITE, DEFAULT_SEEDOFFSET);
}


static size_t maxWrite;
static char **compare;
static struct RandomString *rs;
static const char *filterName;
static uint32_t seedOffset = DEFAULT_SEEDOFFSET;
static const char *seedsString = 0;


int construct(int argc, const char **argv) {

    DSPEW();

    maxWrite = qsOptsGetSizeT(argc, argv,
            "maxWrite", QS_DEFAULTMAXWRITE);

    seedOffset = qsOptsGetUint32(argc, argv,
            "seedStart", seedOffset);

    seedsString = qsOptsGetString(argc, argv,
            "seeds", 0);

    ASSERT(maxWrite);

    filterName = qsGetFilterName();

    return 0; // success
}


int start(uint32_t numInputs, uint32_t numOutputs) {

    ASSERT(numInputs);

    DSPEW("%" PRIu32 " inputs  %" PRIu32 " outputs",
            numInputs, numOutputs);

    if(numInputs > numOutputs) {
        compare = calloc(numInputs - numOutputs, sizeof(*compare));
        ASSERT(compare, "calloc(%" PRIu32 ",%zu) failed",
                numInputs - numOutputs, sizeof(*compare));

        for(uint32_t i=0; i<numInputs - numOutputs; ++i) {
            compare[i] = calloc(1, maxWrite + 1);
            ASSERT(compare[i], "calloc(1,%zu) failed", maxWrite + 1);
        }
    }

    for(uint32_t i=0; i<numOutputs; ++i)
        qsCreateOutputBuffer(i, maxWrite);

    rs = calloc(numInputs, sizeof(*rs));
    ASSERT(rs, "calloc(%" PRIu32 ",%zu) failed",
            numInputs, sizeof(*rs));

    uint32_t seeds[numInputs];
    for(uint32_t i=0; i<numInputs; ++i)
        seeds[i] = i + seedOffset;

    if(seedsString) {
        unsigned int val;
        const char *str = seedsString;
        uint32_t i = 0;
        while(i < numInputs && *str && sscanf(str, "%u", &val) == 1) {
            seeds[i++] = val;
            // Go to the next number in the string str.
            while(*str && (*str >= '0' && *str <= '9')) ++str;
            while(*str && (*str < '0' || *str > '9')) ++str;
        }
    }

    for(uint32_t i=0; i<numInputs; ++i) {
        DSPEW("Filter \"%s\" Input port %" PRIu32 " seed=%" PRIu32,
                qsGetFilterName(), i, seeds[i]);
        // Initialize the random string generator.
        randomString_init(rs + i, seeds[i]);
    }

    return 0; // success
}


int input(void *buffers[], const size_t lens[],
        const bool isFlushing[],
        uint32_t numInputs, uint32_t numOutputs) {

    for(uint32_t i=0; i<numInputs; ++i) {

        size_t len = lens[i];
        if(len > maxWrite)
            len = maxWrite;
        else if(len == 0)
            continue;

        qsAdvanceInput(i, len);

        char *in = buffers[i];
        char *out;

        if(i < numOutputs)
            out = qsGetOutputBuffer(i, len, len);
        else
            // This has no corresponding output for this input port
            // so we use a buffer that we allocated in start().
            out = compare[i - numOutputs];

        randomString_get(rs + i, len, out);

        for(size_t j=0; j<len; ++j)
            // Check each character.  We like to know where it fails.
            ASSERT(out[j] == in[j],
                    "%s Miss-match on input channel %" PRIu32,
                    filterName, i);

        if(i < numOutputs)
            qsOutput(i, len);
    }

    if(numOutputs > numInputs)
        // We have excess outputs.
        for(uint32_t i=numOutputs-numInputs; i<numOutputs; ++i) {

            // Write the last input to the rest of the outputs.
            size_t len = lens[numInputs-1];
            if(len > maxWrite)
                len = maxWrite;
            else if(len == 0)
                continue;
            char *out = qsGetOutputBuffer(i, len, len);
            memcpy(out, buffers[numInputs-1], len);
            // This data that we are outputting was checked already.
            qsOutput(i, len);
        }

    return 0;
}


int stop(uint32_t numInputs, uint32_t numOutputs) {

    DASSERT(rs);
    free(rs);
    rs = 0;

    if(numInputs > numOutputs) {
        for(uint32_t i=0; i<numInputs - numOutputs; ++i) {
            DASSERT(compare[i]);
            free(compare[i]);
            compare[i] = 0;
        }

        DASSERT(compare);
        free(compare);
        compare = 0;
    }

    return 0;
}
