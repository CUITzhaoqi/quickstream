#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <alloca.h>

#include "./qs.h"
#include "../include/qsfilter.h"
#include "./debug.h"

#if 0
static inline void
AdvanceWritePtr(struct QsOutput *output, size_t len) {

    // We are in the correct thread to do this.
    struct QsWriter *writer = output->writer;
    struct QsBuffer *buffer = writer->buffer;

    DASSERT(len <= buffer->overhangLength,
            "len=%zu > buffer->overhangLength=%zu",
            len, buffer->overhangLength);

    if(writer->writePtr + len < buffer->mem + buffer->mapLength)
        writer->writePtr += len;
    else
        writer->writePtr += len - buffer->mapLength;
}


static inline void
AdvanceReadPtr(struct QsOutput *output, size_t len) {

    // We are in the correct thread to do this.
    struct QsBuffer *buffer = output->writer->buffer;

    DASSERT(len <= buffer->overhangLength,
            "len=%zu > buffer->overhangLength=%zu",
            len, buffer->overhangLength);

    if(output->readPtr + len < buffer->mem + buffer->mapLength)
        output->readPtr += len;
    else
        output->readPtr += len - buffer->mapLength;
}
#endif


// Allocate all buffers for all filter in the stream that have not been
// allocated already by the filter start() calling qsBufferCreate().  This
// sets up the default buffering arrangement.
//
void AllocateBuffer(struct QsFilter *f) {

}


// This is called when outputs exist, and after un-mapping memory.
//
void FreeBuffers(struct QsFilter *f) {
    
    DASSERT(f->outputs, "");

}


void MapRingBuffers(struct QsFilter *f) {

    DASSERT(f->outputs || f->numOutputs == 0, "");
    DASSERT(f->numOutputs <= QS_MAX_CHANNELS, "");

}


void UnmapRingBuffers(struct QsFilter *f) {

    DASSERT(f->outputs, "");

}



void qsOutput(uint32_t portNum, const size_t len) {



}

void *qsGetOutputBuffer(uint32_t outputPortNum, size_t maxLen) {

    return 0;
}

void qsAdvanceInput(const size_t lens[]) {

}

void qsSetInputMax(const size_t lens[]) {

}

// Outputs can share the same buffer.  The list of output ports is
// in the QS_ARRAYTERM terminated array outputPortNums[].
//
// Here we just allocate the output buffer structure.  Later we will
// mmap() the ring buffers, after all the filter start()s are called.
//
void qsCreateOutputBuffer(size_t maxWriteLen,
        const uint32_t outputPortNums_in[]) {

    DASSERT(_qsCurrentFilter, "");
    ASSERT(_qsCurrentFilter->numOutputs <= QS_MAX_CHANNELS, "");

}
