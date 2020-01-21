#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#include <pthread.h>

#include "./debug.h"
#include "./qs.h"
#include "./flowJobLists.h"
#include "../include/quickstream/filter.h"



// Called where filter, f, is a source.
//
// This is only called by the master thread.
//
// Create worker threads.  This is called once per filter from the master
// thread.  Source filters can only be feed by themselves.  So the filter,
// f, by design, will not have a worker thread working on it at the time
// of this function call.
//
// This function does not block except for a mutex lock, which should be
// brief and infrequent.
//
static inline
void FeedJobToWorkerThread(struct QsStream *s, struct QsFilter *f) {

    DASSERT(_qsMainThread == pthread_self(), "Not main thread");
    DASSERT(s);
    DASSERT(f);

    /////////////////////////////////////////////////////////////////////
    /////////////////// HAVE STREAM MUTEX ///////////////////////////////
    /////////////////////////////////////////////////////////////////////
    CHECK(pthread_mutex_lock(&s->mutex));

    // We have a job in the filter queue.
    DASSERT(f->stage);
    // We are the first to call this for this source filter, f.
    DASSERT(f->numWorkingThreads == 0);
    DASSERT(f->maxThreads);
    DASSERT(GetNumAllocJobsForFilter(s, f) >= 2);

    // Put the job in the queue that the workers get the jobs from.  In
    // this case, this being the first action on a source filter, there
    // will be jobs available to move from filter unused to filter
    // stage.
    FilterStageToStreamQAndSoOn(s, f);


    if(s->numThreads != s->maxThreads) {
        //
        // Stream does not have its' quota of worker threads.
        //
        pthread_t thread;
        CHECK(pthread_create(&thread, 0/*attr*/,
                    (void *(*) (void *)) RunningWorkerThread, s));

        ++s->numThreads;

        // The new thread may be born and get its' job or another worker
        // thread may finish its' current job and take this job before
        // that new thread can.  So this thread may end up idle.  It's
        // works either way.

    } else if(s->numIdleThreads) {
        //
        // There is a thread calling pthread_cond_wait(s->cond, s->mutex),
        // the s->mutex lock before that guarantees it.
        //
        CHECK(pthread_cond_signal(&s->cond));

        // At least one (and maybe more) worker threads will wake up
        // sometime after we release the s->mutex.  The threads that wake
        // up will handle the numIdleThreads counting.  We are done.
    }

    // else: a worker thread will get the job from the stream queue later.
    // We have no idle threads (that's good) and; we are running as many
    // worker threads as we can.  That may be good too, but running many
    // threads can cause contention, and the more threads the more
    // contention.  Performance measures can determine the optimum number
    // of threads.  Also we need to see how this flow runner method
    // (quickstream) compares to other streaming frame-works.  We need
    // benchmarks.


    CHECK(pthread_mutex_unlock(&s->mutex));
    /////////////////////////////////////////////////////////////////////
    /////////////////// RELEASED STREAM MUTEX ///////////////////////////
    /////////////////////////////////////////////////////////////////////
}



// This function starts the flow by pouring threads into all the source
// filters.
static
uint32_t nThreadFlow(struct QsStream *s) {

    DASSERT(_qsMainThread == pthread_self(), "Not main thread");

    // Set all filter->mark = false
    //
    // filter->mark will be true when the filter is finished.
    // Filters must finish in the order of the filter graph flow.
    //
    // A Filter is flushing when the filter feeding the particular channel
    // is finished.  The filter reads a isFlushing input port when the
    // feeding filter will no longer have input() called.
    StreamSetFilterMarks(s, false);

    for(uint32_t i=0; i<s->numSources; ++i)
        FeedJobToWorkerThread(s, s->sources[i]);

    // Now the worker threads will run wild in the stream.

    DASSERT(s->masterWaiting != true);


    return 0; // success
}


// Allocate the input arguments or so called job arguments.
static inline
void AllocateJobArgs(struct QsFilter *f, struct QsJob *job,
        uint32_t numInputs, uint32_t numOutputs) {

    if(numOutputs) {
        job->outputLens = calloc(numOutputs,
                sizeof(*job->outputLens));
        ASSERT(job->outputLens, "calloc(%" PRIu32 ",%zu) failed",
                numOutputs, sizeof(*job->outputLens));
    }

    if(numInputs == 0) return;

    job->inputBuffers = calloc(numInputs, sizeof(*job->inputBuffers));
    ASSERT(job->inputBuffers, "calloc(%" PRIu32 ",%zu) failed",
            numInputs, sizeof(*job->inputBuffers));
    job->inputLens = calloc(numInputs, sizeof(*job->inputLens));
    ASSERT(job->inputLens, "calloc(%" PRIu32 ",%zu) failed",
            numInputs, sizeof(*job->inputLens));
    job->isFlushing = calloc(numInputs, sizeof(*job->isFlushing));
    ASSERT(job->isFlushing, "calloc(%" PRIu32 ",%zu) failed",
            numInputs, sizeof(*job->isFlushing));
    job->advanceLens = calloc(numInputs, sizeof(*job->advanceLens));
    ASSERT(job->advanceLens, "calloc(%" PRIu32 ",%zu) failed",
            numInputs, sizeof(*job->advanceLens));
}


// This recurses.
//
// This is not called unless s->maxThreads is non-zero.
static
void AllocateFilterJobsAndMutex(struct QsStream *s, struct QsFilter *f) {

    DASSERT(f);
    DASSERT(f->mark);
    DASSERT(f->maxThreads, "maxThreads cannot be 0");
    DASSERT(f->jobs == 0);

    // Un-mark this filter.
    f->mark = false;

    uint32_t numJobs = GetNumAllocJobsForFilter(s, f);
    uint32_t numInputs = f->numInputs;

    f->jobs = calloc(numJobs, sizeof(*f->jobs));
    ASSERT(f->jobs, "calloc(%" PRIu32 ",%zu) failed",
            numJobs, sizeof(*f->jobs));

    DASSERT(f->mutex == 0);
    DASSERT(f->maxThreads != 0);

    if(s->maxThreads > 1 && f->maxThreads > 1) {
        f->mutex = malloc(sizeof(*f->mutex));
        ASSERT(f->mutex, "malloc(%zu) failed", sizeof(*f->mutex));
        CHECK(pthread_mutex_init(f->mutex, 0));
        // Not lock-less buffers, but we have multi-threaded filter
        // input().
    }
    // else: We have lock-less buffers.


    for(uint32_t i=0; i<numJobs; ++i) {

        f->jobs[i].filter = f; 

        AllocateJobArgs(f, f->jobs + i, numInputs, f->numOutputs);
        // Initialize the unused job stack:
        if(i >= 2)
            // Note: the top of f->jobs[] is used as the queue.  It's just
            // how we choice to initialize this data.
            (f->jobs + i - 1)->next = f->jobs + i;
    }
 
    // Initialize the job stage queue with one empty job.
    f->stage = f->jobs;
    // Set the top of the unused job stack.
    f->unused = f->jobs + 1;

    // Am I a stupid-head?
    DASSERT(f->jobs->next == 0);
    DASSERT((f->jobs+numJobs-1)->next == 0);


    // Recurse if we need to.
    for(uint32_t i=0; i<f->numOutputs; ++i) {
        struct QsOutput *output = f->outputs + i;
        for(uint32_t j=0; j<output->numReaders; ++j) {
            struct QsFilter *nextFilter = (output->readers + j)->filter;
            DASSERT(nextFilter);
            if(nextFilter->mark)
                AllocateFilterJobsAndMutex(s, nextFilter);
        }
    }
}


int qsStreamWait(struct QsStream *s) {

    DASSERT(_qsMainThread == pthread_self(), "Not main thread");
    ASSERT(s->sources, "qsStreamReady() must be successfully"
            " called before this");
    ASSERT(s->flags & _QS_STREAM_LAUNCHED,
            "Stream has not been launched");

    if(s->maxThreads == 0)
        // qsStreamLaunch() was not configured to run with worker
        // threads.
        return -1;

    if(s->numThreads == 0)
        // The number of worker threads is 0 and so there is no reason to
        // wait, and no worker threads to signal this main/master thread.
        return 1;


    CHECK(pthread_mutex_lock(&s->mutex));

    DASSERT(s->masterWaiting == false);

    s->masterWaiting = true;
    // The last worker to quit will signal this conditional.
    CHECK(pthread_cond_wait(&s->masterCond, &s->mutex));
    s->masterWaiting = false;
    CHECK(pthread_mutex_unlock(&s->mutex));

    return 0; // yes we did wait.
}


int qsStreamLaunch(struct QsStream *s, uint32_t maxThreads) {

    DASSERT(_qsMainThread == pthread_self(), "Not main thread");

    ASSERT(maxThreads!=0, "Write the code for the maxThread=0 case");
    ASSERT(maxThreads <= _QS_STREAM_MAXMAXTHREADS,
            "maxThread=%" PRIu32 " is too large (> %" PRIu32 ")",
            maxThreads, _QS_STREAM_MAXMAXTHREADS);

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

        CHECK(pthread_mutex_init(&s->mutex, 0));
        CHECK(pthread_cond_init(&s->cond, 0));
        CHECK(pthread_cond_init(&s->masterCond, 0));

        StreamSetFilterMarks(s, true);
        for(uint32_t i=0; i<s->numSources; ++i)
            AllocateFilterJobsAndMutex(s, s->sources[i]);
    }


    ASSERT(s->flow, "Did not set a stream flow function. "
            "Write this code");

    return s->flow(s);
}
