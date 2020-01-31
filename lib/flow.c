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



// Stop running input() for this filter, f.
//
// Mark the filter, f, as done.
// Remove all jobs for this filter, f, from the stream job queue.
//
// We need a stream mutex lock before calling this.
static inline
void StopRunningInput(struct QsStream *s, struct QsFilter *f,
        struct QsJob *j, int inputRet) {

    //
    // This filter is done having input() called for this flow cycle
    // (start(), input(), input(), input(), ..., stop()) so we do not
    // need to mess with input() any more.  The buffers/readers will be
    // reset before the next flow/run cycle, if there is one.  But we
    // still need to deal with output.
    //
    // The decision to stop having input() called was made by the filter
    // in this case.
    //
    // Another way that input() could stop being called is that there is
    // no more input data feeding the filter.
    //
    if(inputRet < 0)
        WARN("filter \"%s\" input() returned error code %d",
                    f->name, inputRet);

    DSPEW("filter \"%s\" input() returned %d"
            " is done with this flow cycle",
            f->name, inputRet);
    // This is currently counted as a working thread.
    DSPEW("filter \"%s\" %" PRIu32 " working input() threads remain",
            f->name, f->numWorkingThreads - 1);

    DASSERT(f->numWorkingThreads >= 1);


    // Mark this filter as being done having it's input() called.
    if(f->mark == 0) {
    
        // This is the first call to this function for this
        // filter.

        // Now we must remove any existing jobs from this filter from the
        // stream job queue.
        //
        // We only do this once for each filter when they finish, so speed
        // is not an issue.
        struct QsJob *next;
        if(f->maxThreads - f->numWorkingThreads > 0 &&
                s->maxThreads - f->numWorkingThreads > 0)
            // There will be no jobs in the stream job queue if "f" is not
            // a multi-threaded filter with jobs in the stream job queue.
            // Filter WorkingThreads are not in the stream job queue.
            for(struct QsJob *job = s->jobFirst; job; job = next) {
                // the job passed to this function, j, is in the filter
                // working queue and not in the stream job queue.
                DASSERT(j != job);
                // We are editing a list while iterating through it; so we
                // save the job->next because if StreamQToFilterUnused(job) is
                // called it will make job->next invalid after it's called,
                // but the job that job->next points to now will still be in
                // the list after we call StreamQToFilterUnused(job).
                next = job->next;
                if(job->filter == f)
                    StreamQToFilterUnused(s, f, job);
            }
        // Mark this filter as being done having it's input() called.
        f->mark = 1;
    }
#ifdef SPEW_LEVEL_DEBUG
    else {
        ++f->mark;
        // Another thread called to input() after we stopped calling
        // input().
        // The filter can have at most this many threads calling input(),
        // so it can only quit this many times.
        DASSERT(f->mark <= f->maxThreads);
    }
#endif
}



// Returns true if the filter, f, has conditions needed for it's input()
// function to be called.
static inline
bool CheckFilterInputCallable(struct QsFilter *f) {

    if(f->unused == 0 || f->mark) 
        // This filter has a full amount of working threads already.
        return false;

    // If any outputs are clogged than we cannot call input() for filter
    // f.
    for(uint32_t i=f->numOutputs-1; i!=-1; --i) {
        struct QsOutput *output = f->outputs + i;
        for(uint32_t j=output->numReaders-1; j!=-1; --j) {
            struct QsReader *reader = output->readers + j;
            struct QsFilter *rf = reader->filter;
            uint32_t inPort = reader->inputPortNum;
            if(rf->stage->inputLens[inPort] >= output->maxLength)
                // We have at least one clogged output reader.  It has a
                // full amount that it can read.  And so we will not be
                // able to call input().  Otherwise we could overrun the
                // read pointer with the write pointer.
                return false;
        }
    }

    // If any input has meets the threshold we can call input() for this
    // filter, f.
    for(uint32_t i=f->numInputs-1; i!=-1; --i)
        if(f->stage->inputLens[i] >= f->readers[i]->threshold)
            return true;

    if(f->numInputs == 0)
        // We have no inputs so the inputs are not a restriction.
        return true;

    return false;
}



//
// Run input().
//
// Advance write and read buffer pointers.
//
// Spawn jobs to other filters.
//
// Returns true to signal call me again, and returns without a stream
// mutex lock.
//
// Returns false if we do not want to call it again and also returns with
// a stream mutex lock.
static inline
bool RunInput(struct QsStream *s, struct QsFilter *f, struct QsJob *j) {

    // At this point this filter/thread owns this job.
    //
    int inputRet;

    inputRet = f->input(j->inputBuffers, j->inputLens,
            j->isFlushing, f->numInputs, f->numOutputs);


    // STREAM LOCK
    CHECK(pthread_mutex_lock(&s->mutex));

    CheckLockFilter(f);

    // Note: all these for loop iteration is through just the number of
    // inputs and outputs to and from the filter.  Usually there'll be
    // just 1 or 2 inputs and 1 or 2 outputs.
    //
    // As this code matures maybe we can mash together more of these for
    // loops.


    // Both the inputs and the outputs must be "OK" in order to call
    // input() after this call.
    //
    // inputsFeeding will be changed to true is one input threshold is
    // met.
    bool inputsFeeding = false;
    // outputsHungry will be changed to false if one output is full.
    bool outputsHungry = true;


    // Advance the output write pointers and see if we can write more.
    //
    // To be able to write more we must be able to write maxLength to all
    // output readers; because that's what this API promises the filter it
    // can do.
    //
    // And grow the reader filters stage jobs.
    for(uint32_t i=f->numOutputs-1; i!=-1; --i) {

        struct QsOutput *output = f->outputs + i;

        // This should have been checked already in qsGetOutputBuffer()
        // and in qsOutput() hence the DASSERT() and not ASSERT().  Check
        // that the filter, f, did not write more than promised.
        DASSERT(j->outputLens[i] <= output->maxWrite,
                "Filter \"%s\" wrote %zu which is greater"
                " than the %zu promised",
                f->name, j->outputLens[i], output->maxWrite);

        // Advance write pointer.
        output->writePtr += j->outputLens[i];
        if(output->writePtr >= output->buffer->end)
            output->writePtr -= output->buffer->mapLength;

        for(uint32_t k=output->numReaders-1; k!=-1; --k) {
            struct QsReader *reader = output->readers + k;
            struct QsFilter *rf = reader->filter;
            uint32_t inPort = reader->inputPortNum;
            if(rf->stage->inputLens[inPort] >= output->maxLength &&
                    outputsHungry)
                // We have at least one clogged output reader.  It has
                // a full amount that it can read.  And so we will not
                // be continuing to call input().  Otherwise we could
                // overrun the read pointer with the write pointer.
                outputsHungry = false;
  
            // Grow the reader filter's stage job.
            rf->stage->inputLens[inPort] += j->outputLens[i];
        }
    }


    // Advance the read pointers that feed this filter, f; and tally the
    // readers remaining length.
    for(uint32_t i=f->numInputs-1; i!=-1; --i) {
        struct QsReader *r = f->readers[i];
        // This should have been checked in qsAdvanceInput()

        DASSERT(j->advanceLens[i] <= r->maxRead);
        DASSERT(j->advanceLens[i] <= j->inputLens[i]);
        DASSERT(j->inputLens[i] == r->readLength);

        if(r->readLength >= r->maxRead)
            ASSERT(j->advanceLens[i],
                    "The filter \"%s\" did not keep it's read promise",
                    f->name);

        r->readPtr += j->advanceLens[i];
        r->readLength -= j->advanceLens[i];
        if(r->readPtr >= r->buffer->end)
            // Wrap the circular buffer back toward the start.
            r->readPtr -= r->buffer->mapLength;
    }


    if(outputsHungry)
        // Check if we have the any required input data thresholds,
        // but there's no point if an output reader is clogged;
        // hence the if(outputHungry).
        //
        for(uint32_t i=f->numInputs-1; i!=-1; --i) {
            if(f->readers[i]->readLength >= f->readers[i]->threshold) {
                // The amount of input data left meets the needed
                // threshold in at least one input.  If the threshold
                // condition if more complex than the filter
                inputsFeeding = true;
                break;
            }
        }

    
    uint32_t numAddedJobs = 0;

    // Add jobs to the stream job queue if we can, for filters we are
    // feeding.
    for(uint32_t i=f->numOutputs-1; i!=-1; --i) {
        struct QsOutput *output = f->outputs + i;
        for(uint32_t k=output->numReaders-1; k!=-1; --k)
            if(CheckFilterInputCallable(output->readers[k].filter)) {
                FilterStageToStreamQAndSoOn(s, f);
                ++numAddedJobs;
            }
    }

    //  Add jobs to the stream job queue for source filters if there are
    //  more threads available.
    if(numAddedJobs < s->maxThreads - s->numThreads + s->numIdleThreads)
        for(uint32_t i=s->numSources-1; i!=-1; --i)
            if(CheckFilterInputCallable(s->sources[i])) {
                FilterStageToStreamQAndSoOn(s, s->sources[i]);
                ++numAddedJobs;
            }


    // num is number of threads that are idle that we will wake up.
    uint32_t num = numAddedJobs;
    if(num >= s->numIdleThreads)
        // Wait all idle worker threads.
        CHECK(pthread_cond_broadcast(&s->cond));
    else if(num)
        // Wait just some worker threads.
        while(num--)
            CHECK(pthread_cond_signal(&s->cond));
    // Note: the idle threads do not wait up until after we unlock the
    // stream mutex.


    // num is now reused as the number of threads to create
    num = 0;
    if(numAddedJobs > s->numIdleThreads)
        num = numAddedJobs - s->numIdleThreads;
    if(num > s->maxThreads - s->numThreads)
        num = s->maxThreads - s->numThreads;

    if(num)
        while(num--)
            LaunchWorkerThread(s);

    bool ret = true;

    if(inputRet || f->mark) {
        ret = false;
        StopRunningInput(s, f, j, inputRet);
    }


    if(ret && outputsHungry && inputsFeeding) {
        // We will be calling input() again.

        // Set up the job, j, for another input call:
        //
        for(uint32_t i=f->numInputs-1; i!=-1; --i) {

            j->inputLens[i] = f->readers[i]->readLength;
            j->advanceLens[i] = 0;
            j->inputBuffers[i] = f->readers[i]->readPtr;
        }

        for(uint32_t i=f->numOutputs-1; i!=-1; --i)
            j->outputLens[i] = 0;

    } else if(ret)
        ret = false;

    CheckUnlockFilter(f);


    if(ret)
        // This will be called again and we do not need a
        // stream mutex lock at the start of this function.
        //
        // STREAM UNLOCK
        CHECK(pthread_mutex_unlock(&s->mutex));
    // else
    //    We return with the STREAM LOCK

    return ret;
}



// We require a stream mutex lock before calling this.
//
// This function returns while holding the stream mutex lock.
//
// returns 0 if all threads would be sleeping.
struct QsJob *GetWork(struct QsStream *s) {

#ifdef SPEW_LEVEL_DEBUG
    // So we may spew when the number of working threads changes.
    bool weSlept = false;
#endif

    while(true) {
    
        // We are unemployed.  We have no job.  Just like I'll be, after I
        // finish writing this code.

        // Get the next job (j) from the stream job queue is there is
        // one.
        //
        struct QsJob *j = StreamQToFilterWorker(s);

#ifdef SPEW_LEVEL_DEBUG
        // We only spew if the number of working threads has changed and
        // if we slept than the number of working threads has changed.
        if(weSlept && j)
            DSPEW("Now %" PRIu32 " out of %" PRIu32
                "thread(s) waiting for work",
                s->numIdleThreads, s->numThreads);
        else if(j == 0)
            weSlept = true;
#endif

        if(j) return j;

        if(s->numIdleThreads == s->numThreads - 1)
            // All other threads are idle so we are done working/living.
            return 0;

        // We count ourselves in the ranks of the sleeping unemployed.
        ++s->numIdleThreads;

        // TODO: all the spewing in this function may need to be removed.
        DSPEW("Now %" PRIu32 " out of %" PRIu32
                "thread(s) waiting for work",
                s->numIdleThreads, s->numThreads);

        // STREAM UNLOCK  -- at wait
        // wait
        CHECK(pthread_cond_wait(&s->cond, &s->mutex));
        // STREAM LOCK  -- when woken.

        // Remove ourselves from the numIdleThreads.
        --s->numIdleThreads;

        if(s->numIdleThreads == s->numThreads - 1) {
            // All other threads are idle so we are done working/living.
            //
            // In order for this to happen there should be no jobs in the
            // stream job queue.
            DASSERT(StreamQToFilterWorker(s) == 0);
            return 0;
        }
    }
}



// This is the first function called by worker threads.
//
void *RunningWorkerThread(struct QsStream *s) {

    DASSERT(s);
    DASSERT(s->maxThreads);

    // The life of a worker thread.

    // STREAM LOCK
    CHECK(pthread_mutex_lock(&s->mutex));

    // The thing that created this thread must have counted the number of
    // threads in the stream, otherwise if we counted it here and there
    // are threads that are slow to start, than the master thread could
    // see less threads than there really are.  So s->numThreads can't
    // be zero now.
    DASSERT(s->numThreads);
    // The number of threads must be less than or equal to stream
    // maxThreads.  We have the needed stream mutex lock to check this.
    DASSERT(s->numThreads <= s->maxThreads);


    DSPEW("The %" PRIu32 " (out of %" PRIu32
            " worker threads are running.",
            s->numThreads, s->maxThreads);

    struct QsJob *j;

    // We work until we die.
    //
    while((j = GetWork(s))) {

        // This worker has a new job.

        // STREAM UNLOCK
        CHECK(pthread_mutex_unlock(&s->mutex));
 
        struct QsFilter *f = j->filter;
        DASSERT(f);


        // To access the readPtr and readLength in multi-threaded filters
        // we need a mutex lock.  If f is not a multi-threaded filter than
        // this does nothing.
        CheckLockFilter(f);

        // We need to set get the current read pointer into the current
        // job, j and find the total length that can be read.
        for(uint32_t i=f->numInputs; i!=-1; --i) {
            // Add leftover unread length to the length that
            // the feeding filters have added since the last
            // time this filter had input() called.
            j->inputLens[i] += f->readers[i]->readLength;
            // We now make the two consistent.
            f->readers[i]->readLength = j->inputLens[i];
        }

        // Ya, undo that lock.
        CheckUnlockFilter(f);


        // This thread can now read and write to this job, j, without a
        // mutex.  No other thread will access this job while it is
        // here.
        //
        // Put it in this thread specific data so we can find it in the
        // filter input() when this thread calls functions like
        // qsAdvanceInput(), qsOutput(), and other quickstream/filter.h
        // functions.
        //
        CHECK(pthread_setspecific(_qsKey, j));


        // call input() as many times as we can; until it's starved for
        // input data or any output is clogged.
        while(RunInput(s, f, j));


        // STREAM LOCK
        CHECK(pthread_mutex_lock(&s->mutex));


        // Move this job structure to the filter unused stack.
        FilterWorkingToFilterUnused(j);
    }

    // This thread is now no longer counted among the working/living.
    --s->numThreads;

    // Now this thread does not count in s->numThreads or
    // s->numIdleThreads
    if(s->numThreads && s->numIdleThreads == s->numThreads) {

        // Wake up the all these lazy workers so they can return.
        CHECK(pthread_cond_broadcast(&s->cond));
        // TODO: pthread_cond_broadcast() does nothing if it was called by
        // another thread on the way out.  Maybe add a flag so we do not
        // call it more than once in this case.
    }

    if(s->numThreads == 0 && s->masterWaiting)
        // this last thread out signals the master.
        CHECK(pthread_cond_broadcast(&s->masterCond));


    // STREAM UNLOCK
    CHECK(pthread_mutex_unlock(&s->mutex));


    return 0; // We're dead now.  It was a good life for a worker/slave.
}
