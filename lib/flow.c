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



// Called by the main/manager thread.
//
// Transfer job from the filter->queue to the stream job queue, then
// transfer a job from filter unused to filter queue, and then clean the
// filter queue job args.
//
// We must have a stream->mutex lock to call this.
static inline
void FilterQToStreamQAndSoOn(struct QsStream *s, struct QsJob *j) {

    DASSERT(s);
    DASSERT(j->next == 0);
    struct QsFilter *f = j->filter;
    DASSERT(f);

    /////////////////////////////////////////////////////////////////////
    // 1. Transfer job from the filter->queue to the stream job queue.

    if(s->jobLast) {
        // There are jobs in the stream queue.
        DASSERT(s->jobLast->next == 0);
        DASSERT(s->jobFirst);
        s->jobLast->next = j;
        s->jobLast = j;
    } else {
        // There are no jobs in the stream queue.
        DASSERT(s->jobFirst == 0);
        s->jobLast = s->jobFirst = j;
    }
    // We are done with j.  We can reuse it.

    /////////////////////////////////////////////////////////////////////
    // 2. Now transfer a job from filter unused to filter queue.

    DASSERT(f->unused);
    j = f->unused;
    f->unused = j->next;


    /////////////////////////////////////////////////////////////////////
    // 3. Now clean/reset the filter queue job args.

    if(f->numInputs) {
        // This is not a source filter.
        DASSERT(j->inputBuffers);
        DASSERT(j->inputLens);
        DASSERT(j->isFlushing);
        DASSERT(j->advanceLens);

        memset(j->inputBuffers, 0, f->numInputs*sizeof(*j->inputBuffers));
        memset(j->inputLens, 0, f->numInputs*sizeof(*j->inputLens));
        // TODO: Deal with isFlushing ....
        memset(j->isFlushing, 0, f->numInputs*sizeof(*j->isFlushing));
        memset(j->advanceLens, 0, f->numInputs*sizeof(*j->advanceLens));
    }
#ifdef DEBUG
    else {
        // It must be a source filter.  There is nothing to clean/reset.
        DASSERT(j->inputBuffers == 0);
        DASSERT(j->inputLens == 0);
        DASSERT(j->isFlushing == 0);
        DASSERT(j->advanceLens == 0);
    }
#endif
}


// This is called by the worker threads.
//
// Transfer job from the stream job queue to the worker thread function
// call stack.
//
// We must have a stream->mutex lock to call this.
static inline
struct QsJob *StreamQToWorker(struct QsStream *s) {

    DASSERT(s);

    if(s->jobFirst) {
        struct QsJob *j = s->jobFirst;
        DASSERT(s->jobLast);
        DASSERT(s->jobLast->next == 0);
        s->jobFirst = j->next;
        if(s->jobFirst == 0) {
            DASSERT(s->jobLast == j);
            s->jobLast = 0;
            // There are now no jobs in the stream job queue.
        } else {
            // There are still some jobs in the stream job queue.
            DASSERT(j->next);
            j->next = 0;
        }
        return j;
    }
    return 0;
}


// This is called by the worker threads.
//
// Transfer job from the worker thread function call stack to the filter
// unused stack.
//
// We must have a stream->mutex lock to call this.
static inline
void WorkerToFilterUnused(struct QsStream *s, struct QsJob *j) {

    DASSERT(s);
    DASSERT(j->next == 0);
    struct QsFilter *f = j->filter;
    DASSERT(f);

    if(f->unused)
        j->next = f->unused;
    else
        f->unused = j;

    // The job args will be cleaned later up in FilterUnusedToFilterQ().
}



// This is called by worker threads.
//
// Transfer job from the worker call stack to the filter unused stack
//
// We must have a stream->mutex lock to call this.
//static
void *StartThread(struct QsStream *s) {

    // The life of a worker thread.

    DASSERT(s);

    bool living = true;

    // LOCK
    CHECK(pthread_mutex_lock(&s->mutex));


    while(living) {

        // Get the next job from the stream job queue and put it in this
        // thread specific data so we can find it in the filter when this
        // thread runs things like qsOutput() and other filter API
        // functions.
        //
        struct QsJob *j =  StreamQToWorker(s);

        // This thread can now read and write to the args in this job
        // without a mutex.  No other thread can access this job while it
        // is not in a stream queue, filter queue, or filter unused
        // list.

        if(j) {

            // We have a job.

            CHECK(pthread_setspecific(_qsKey, j));
            struct QsFilter *f = j->filter;
            DASSERT(f->numThreads <= f->maxThreads);

            // UNLOCK
            CHECK(pthread_mutex_unlock(&s->mutex));

            int ret = f->input(j->inputBuffers, j->inputLens,
                    j->isFlushing,
                    f->numInputs, f->numOutputs);

            if(ret) {
                if(ret < 0)
                    WARN("filter \"%s\" input() returned error code %d",
                            f->name, ret);


            }


DSPEW("ret=%d", ret);

            // LOCK
            CHECK(pthread_mutex_lock(&s->mutex));


        } else {

            // We are unemployed.

            // UNLOCK
            CHECK(pthread_cond_wait(&s->cond, &s->mutex));
            // LOCK

        }

    }

    // UNLOCK
    CHECK(pthread_mutex_unlock(&s->mutex));

    return 0;
}




// This appends the job that is in the filter
//
// This is for multi-thread flow.
//
// Recursion is possible with inline functions; the functions just get
// unrolled a few times.
// TODO: How can I have inline unroll this recursive function just a few
// times.  It's command option: --max-inline-insns-recursive
//
static inline
void InputToThread(struct QsStream *s, struct QsFilter *f) {

    DASSERT(s);
    DASSERT(f);

    /////////////////////////////////////////////////////////////////////
    /////////////////// HAVE STREAM MUTEX ///////////////////////////////
    /////////////////////////////////////////////////////////////////////
    CHECK(pthread_mutex_lock(&s->mutex));

    DASSERT(f->queue);
    DASSERT(f->numThreads <= f->maxThreads);

    // Some how get a worker thread to do our thread input() call.
    //
    FilterQToStreamQAndSoOn(s, f->queue);

    if(s->numIdleThreads) {
        //
        // Note: this job launch mode adds thread contention.
        //
        // There is a thread calling pthread_cond_wait(s->cond, s->mutex),
        // the s->mutex lock before that guarantees it.
        //
        CHECK(pthread_cond_signal(&s->cond));

        // At least one (and maybe more) worker threads will wake up
        // sometime after we release the s->mutex.  The threads that wake
        // up will handle the numIdleThreads counting.  We are done.
    
    } else if(f->numThreads != f->maxThreads) {
        //
        // Filter does not have its' quota of worker threads.
        //
        pthread_t thread;
        CHECK(pthread_create(&thread, 0/*attr*/,
                    (void *(*) (void *)) StartThread, s));

        // The new thread may be born and get its' job or another worker
        // thread may finish its' current job and take this job before
        // that new thread can.  So this thread may end up idle.  It's
        // okay either way.
    }

    // else: We have no idle threads (that's good) and; we are running as
    // many worker threads as we can.  That may be good too, but running
    // many threads can cause contention, and the more threads the more
    // contention.  Performance measures can determine the optimum number
    // of threads.  Also we need to see how this flow runner method
    // (quickstream) compares to other streaming frame-works.


    CHECK(pthread_mutex_unlock(&s->mutex));
    /////////////////////////////////////////////////////////////////////
    /////////////////// RELEASED STREAM MUTEX ///////////////////////////
    /////////////////////////////////////////////////////////////////////

}


static
uint32_t nThreadFlow(struct QsStream *s) {

    for(uint32_t i=0; i<s->numSources; ++i)
        InputToThread(s, s->sources[i]);

    return 0; // success
}


// Allocate the input arguments or so called job arguments.
static inline
void AllocateJobArgs(struct QsJob *job, uint32_t numInputs) {

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
void AllocateFilterJobs(struct QsFilter *f) {

    DASSERT(f);
    DASSERT(f->mark);
    DASSERT(f->maxThreads, "maxThreads cannot be 0");
    DASSERT(f->jobs == 0);

    // Un-mark this filter.
    f->mark = false;

    for(uint32_t i=0; i<f->numOutputs; ++i) {
        struct QsOutput *o = f->outputs + i;
        if(o->prev == 0) {
            // This is a buffer that is owned by this filter and is
            // not a "pass through" buffer, but one that feeds a "pass
            // through" buffer.
            DASSERT(o->mutex == 0);
            o->mutex = malloc(sizeof(*o->mutex));
            ASSERT(o->mutex, "malloc(%zu) failed", sizeof(*o->mutex));
            CHECK(pthread_mutex_init(o->mutex, 0));
        }
#ifdef DEBUG
        else {
            // This is a "pass through" buffer that is fed by another
            // output like above.  It does not need a mutex.
            while(o->prev)
                o = o->prev;
            // o is now the "feed" output for the pass through buffer.
            DASSERT(o->mutex);
        }
#endif
    }

    uint32_t numJobs = f->maxThreads + 1;
    uint32_t numInputs = f->numInputs;

    f->jobs = calloc(numJobs, sizeof(*f->jobs));
    ASSERT(f->jobs, "calloc(%" PRIu32 ",%zu) failed",
            numJobs, sizeof(*f->jobs));

    for(uint32_t i=0; i<numJobs; ++i) {

        f->jobs[i].filter = f; 

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
