#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#include <pthread.h>

#include "./qs.h"
#include "../include/qsfilter.h"
#include "./debug.h"


#if 0

static void *StartThread(struct QsFilter *f) {

    DASSERT(f);
    DASSERT(f->app);

    // This data will exist so long as this thread uses this call stack.
    struct QsThreadData threadData;
    memset(&threadData, 0, sizeof(threadData));

    CHECK(pthread_setspecific(f->app->key, &threadData));

    return 0;
}

static void *FinishThread(void *ptr) {


    return 0;
}


static void CreateThread(struct QsStream *s, struct QsFilter *f) {

    pthread_t thread;

    CHECK(pthread_create(&thread, 0,
                (void *(*) (void *)) StartThread, f));


}

#endif

// The output, o, is one of the output from a filter feeding filter f.
//
// If output o is 0 then f is a source filter.
//
// This is for multi-thread flow.
//
void AppendOutputToFilterJobs(struct QsOutput *o, struct QsFilter *f) {

    // TODO: remove some of these debug asserts after testing.
    DASSERT(_qsMainThread == pthread_self(), "Not main thread");
    DASSERT(f);
    DASSERT(f->input);
    DASSERT(f->queue);
    DASSERT(f->mutex);

    struct QsSream *s;
    s = f->stream;
    DASSERT(s);

    if(o) {
        DSPEW("%p", f->queue);
    } else {
        // This f is a source
    }

    CHECK(pthread_mutex_lock(&s->mutex));

    if(s->job) {

        // There should be no worker threads waiting for work.

        // Wait for the last job to get picked up by a worker thread.
        CHECK(pthread_cond_wait(&s->cond, &s->mutex));

    }

    s->job = f->queue;

    if(numIdleThreads) {
        // Watch out for "spurious wakeup" which is where more than one
        // thread can wait up from one pthread_cond_signal() call.
        //
        // send job to the idle thread.  Any idle thread will do.
        CHECK(pthread_cond_signal(&s->cond));

    } else if(s->numThreads < s->maxThreads) {

        // make a new thread and give is this one stream job.
        pthread_t thread;
        struct QsThread *td;
        td = calloc(1,sizeof(*td));
        ASSERT(td, "calloc(1,%zu) failed", sizeof(*td));
        td->stream = s;

        CHECK(pthread_create(&thread, 0, StartThread, td));



    } else {

        // We must wait for a working thread to take this job after they
        // finish their current job.
    }

    CHECK(pthread_mutex_unlock(&f->stream->mutex));
}


static
uint32_t nThreadFlow(struct QsStream *s) {

    for(uint32_t i=0; i<s->numSources; ++i) {

        AppendOutputToFilterJobs(0, s->sources[i]);


    }

    return 0; // success
}


// Allocate the input arguments or so called job arguments.
static inline
void AllocateJobArgs(struct QsJob *job, uint32_t numInputs) {

    if(numInputs == 0) return;

    job->buffers = calloc(numInputs, sizeof(*job->buffers));
    ASSERT(job->buffers, "calloc(%" PRIu32 ",%zu) failed",
            numInputs, sizeof(*job->buffers));
    job->lens = calloc(numInputs, sizeof(*job->lens));
    ASSERT(job->lens, "calloc(%" PRIu32 ",%zu) failed",
            numInputs, sizeof(*job->lens));
    job->isFlushing = calloc(numInputs, sizeof(*job->isFlushing));
    ASSERT(job->isFlushing, "calloc(%" PRIu32 ",%zu) failed",
            numInputs, sizeof(*job->isFlushing));
}


// This recurses.
//
// This is not called unless s->maxThreads is non-zero.
static
void AllocateFilterJobs(struct QsFilter *f) {

    DASSERT(f);
    DASSERT(f->mark);
    DASSERT(f->maxThreads, "maxThreads cannot be 0");
    DASSERT(f->jobs == 0);
    DASSERT(f->mutex == 0);

    // Un-mark this filter.
    f->mark = false;

    f->mutex = malloc(sizeof(*f->mutex));
    ASSERT(f->mutex, "malloc(%zu) failed", sizeof(*f->mutex));
    CHECK(pthread_mutex_init(f->mutex, 0));


    uint32_t numJobs = f->maxThreads + 1;
    uint32_t numInputs = f->numInputs;

    f->jobs = calloc(numJobs, sizeof(*f->jobs));
    ASSERT(f->jobs, "calloc(%" PRIu32 ",%zu) failed",
            numJobs, sizeof(*f->jobs));

    for(uint32_t i=0; i<numJobs; ++i) {

        AllocateJobArgs(f->jobs + i, numInputs);
        // Initialize the unused job stack:
        if(i >= 2)
            // Note: the top of f->jobs[] is used as the queue.  It's just
            // how we choice to initialize this data.
            (f->jobs + i - 1)->next = f->jobs + i;
    }
 
    // Initialize the job queue with one empty job.
    f->queue = f->jobs;
    // Set the top of the unused job stack.
    f->unused = f->jobs + 1;

    // I'm I a stupid-head?
    DASSERT(f->jobs->next == 0);
    DASSERT((f->jobs+numJobs-1)->next == 0);


    // Recurse if we need to.
    for(uint32_t i=0; i<f->numOutputs; ++i) {
        struct QsOutput *output = f->outputs + i;
        for(uint32_t j=0; j<output->numReaders; ++j) {
            struct QsFilter *nextFilter = (output->readers + j)->filter;
            DASSERT(nextFilter);
            if(nextFilter->mark)
                AllocateFilterJobs(nextFilter);
        }
    }
}


int qsStreamLaunch(struct QsStream *s, uint32_t maxThreads) {

    ASSERT(maxThreads!=0, "Write the code for the maxThread=0 case");
    ASSERT(maxThreads <= _QS_STREAM_MAXMAXTHREADS,
            "maxThread=%" PRIu32 " is too large (> %" PRIu32 ")",
            maxThreads, _QS_STREAM_MAXMAXTHREADS);

    DASSERT(_qsMainThread == pthread_self(), "Not main thread");
    DASSERT(s);
    DASSERT(s->app);
    ASSERT(s->sources, "qsStreamReady() must be successfully"
            " called before this");
    ASSERT(!(s->flags & _QS_STREAM_LAUNCHED),
            "Stream has been launched already");

    DASSERT(s->numSources);

    s->flags |= _QS_STREAM_LAUNCHED;

    s->maxThreads = maxThreads;

    if(s->maxThreads) {

        // Set a stream flow function.
        s->flow = nThreadFlow;

        CHECK(pthread_cond_init(&s->cond, 0));
        CHECK(pthread_mutex_init(&s->mutex, 0));

        StreamSetFilterMarks(s, true);
        for(uint32_t i=0; i<s->numSources; ++i)
            AllocateFilterJobs(s->sources[i]);
    }


    ASSERT(s->flow, "Did not set a stream flow function. "
            "Write this code");

    return s->flow(s);
}
