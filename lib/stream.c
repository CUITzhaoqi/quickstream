#include <stdlib.h>
#include <string.h>
#include <pthread.h>

// The public installed user interfaces:
#include "../include/quickstream/app.h"
#include "../include/quickstream/filter.h"

// Private interfaces.
#include "./debug.h"
#include "./qs.h"



// TEMPORARY DEBUGGING // TODELETE
#define SPEW(fmt, ... )\
    fprintf(stderr, "%s:line=%d: " fmt "\n", __FILE__,\
            __LINE__, ##__VA_ARGS__)
//#define SPEW(fmt, ... ) /* empty macro */


// See comments in qs.h
//
struct QsFilter *_qsCurrentFilter = 0;



struct QsStream *qsAppStreamCreate(struct QsApp *app) {

    DASSERT(_qsMainThread == pthread_self(), "Not main thread");
    DASSERT(app);

    struct QsStream *s = calloc(1, sizeof(*s));
    ASSERT(s, "calloc(1, %zu) failed", sizeof(*s));

    // Add this stream to the end of the app list
    //
    struct QsStream *S = app->streams;
    if(S) {
        while(S->next) S = S->next;
        S->next = s;
    } else
        app->streams = s;

    s->app = app;
    s->flags = _QS_STREAM_DEFAULTFLAGS;

    return s;
}


void qsStreamAllowLoops(struct QsStream *s, bool doAllow) {

    DASSERT(_qsMainThread == pthread_self(), "Not main thread");
    DASSERT(s);
    DASSERT(!(s->flags & _QS_STREAM_START), "We are starting");
    DASSERT(!(s->flags & _QS_STREAM_STOP), "We are stopping");

    // s->sources != 0 at start and stop and this should be a good enough
    // check, if this code is consistent.
    ASSERT(s->sources == 0,
            "The stream is in the wrong mode to call this now.");


    if(doAllow)
        s->flags |= _QS_STREAM_ALLOWLOOPS;
    else
        s->flags &= ~_QS_STREAM_ALLOWLOOPS;
}


static inline void CleanupStream(struct QsStream *s) {

    DASSERT(s);
    DASSERT(s->app);
    DASSERT(s->numConnections || s->connections == 0);

    if(s->sources) {
        DASSERT(s->numSources);
#ifdef DEBUG
        memset(s->sources, 0, sizeof(*s->sources)*s->numSources);
#endif
        free(s->sources);
    }

    if(s->numConnections) {

        DASSERT(s->connections);
#ifdef DEBUG
        memset(s->connections, 0, sizeof(*s->connections)*s->numConnections);
#endif
        free(s->connections);
        s->connections = 0;
        s->numConnections = 0;
    }
#ifdef DEBUG
    memset(s, 0, sizeof(*s));
#endif
    free(s);
}



void qsStreamConnectFilters(struct QsStream *s,
        struct QsFilter *from, struct QsFilter *to,
        uint32_t fromPortNum, uint32_t toPortNum) {

    DASSERT(_qsMainThread == pthread_self(), "Not main thread");

    DASSERT(s);
    DASSERT(s->app);
    ASSERT(from != to, "a filter cannot connect to itself");
    DASSERT(from);
    DASSERT(to);
    DASSERT(to->app);
    DASSERT(from->app == s->app, "wrong app");
    DASSERT(to->app == s->app, "wrong app");
    ASSERT(to->stream == s || !to->stream,
            "filter cannot be part of another stream");
    ASSERT(from->stream == s || !from->stream,
            "filter cannot be part of another stream");
    DASSERT(!(s->flags & _QS_STREAM_START), "We are starting");
    ASSERT(!(s->flags & _QS_STREAM_STOP), "We are stopping");
    ASSERT(s->sources == 0,
            "stream is in the wrong mode to call this now.");


    // Grow the connections array:
    //
    uint32_t numConnections = s->numConnections;
    s->connections = realloc(s->connections,
            (numConnections+1)*sizeof(*s->connections));
    ASSERT(s->connections, "realloc(,%zu) failed",
            (numConnections+1)*sizeof(*s->connections));
    //
    s->connections[numConnections].from = from;
    s->connections[numConnections].to = to;
    s->connections[numConnections].fromPortNum = fromPortNum;
    s->connections[numConnections].toPortNum = toPortNum;
    //
    s->connections[numConnections].from->stream = s;
    s->connections[numConnections].to ->stream = s;
    //
    ++s->numConnections;
}



static inline void RemoveConnection(struct QsStream *s, uint32_t i) {

    DASSERT(s);
    DASSERT(i < -1);
    DASSERT(s->numConnections > i);

    // This is how many connections there will be after this function
    --s->numConnections;

    struct QsFilter *from = s->connections[i].from;
    struct QsFilter *to = s->connections[i].to;

    DASSERT(from);
    DASSERT(to);
    DASSERT(from->stream == s);
    DASSERT(to->stream == s);
    // Outputs should not be allocated yet.
    DASSERT(from->outputs == 0);
    DASSERT(to->outputs == 0);
    DASSERT(from->numOutputs == 0);
    DASSERT(to->numOutputs == 0);

    if(s->numConnections == 0) {

        // Singular case.  We have no connections anymore.
        //
        from->stream = 0;
        to->stream = 0;
        //
        free(s->connections);
        s->connections = 0;
        return;
    }


    // At index i shift all the connections back one, overwriting the i-th
    // one.
    for(;i<s->numConnections; ++i)
        s->connections[i] = s->connections[i+1];

    // Shrink the connections arrays by one
    s->connections = realloc(s->connections,
            (s->numConnections)*sizeof(*s->connections));
    ASSERT(s->connections, "realloc(,%zu) failed",
            (s->numConnections)*sizeof(*s->connections));

    // If this "from" filter is not in this stream mark it as such.
    for(i=0; i<s->numConnections; ++i)
        if(s->connections[i].from == from || s->connections[i].to == from)
            break;
    if(i==s->numConnections)
        // This "from" filter has no stream associated with it:
        from->stream = 0;

    // If the "to" filter is not in this stream mark it as such.
    for(i=0; i<s->numConnections; ++i)
        if(s->connections[i].from == to || s->connections[i].to == to)
            break;
    if(i==s->numConnections)
        // This "to" filter has no stream associated with it:
        to->stream = 0;
}



int qsStreamRemoveFilter(struct QsStream *s, struct QsFilter *f) {

    DASSERT(_qsMainThread == pthread_self(), "Not main thread");
    DASSERT(s);
    DASSERT(f);
    DASSERT(f->stream == s);
    DASSERT(s->numConnections || s->connections == 0);
    DASSERT(!(s->flags & _QS_STREAM_START), "We are starting");
    ASSERT(!(s->flags & _QS_STREAM_STOP), "We are stopping");
    ASSERT(s->sources == 0,
            "stream is in the wrong mode to call this now.");

    bool gotOne = false;

    for(uint32_t i=0; i<s->numConnections;) {
        DASSERT(s->connections);
        DASSERT(s->connections[i].to);
        DASSERT(s->connections[i].from);
        DASSERT(s->connections[i].to != s->connections[i].from);
        //
        if(s->connections[i].from == f || s->connections[i].to == f) {
            RemoveConnection(s, i);
            // s->numConnections just got smaller by one
            // and the current index points to the next
            // element, so we do not increment i.
            gotOne = true;
        } else
            // regular increment i to next element:
            ++i;
    }

    // return 0 == success found and removed a connection, 1 to fail.
    return gotOne?0:1;
}



// Note: this is called FreeFilterRunResources; it does not free up all
// filter resources, just some things that could change between stop() and
// start().
//
static inline
void FreeFilterRunResources(struct QsFilter *f) {

    if(f->jobs) {

        DASSERT(f->stream);
        DASSERT(f->stream->maxThreads);

        uint32_t numJobs = GetNumAllocJobsForFilter(f->stream, f);

        if(f->numInputs) {
            for(uint32_t i=0; i<numJobs; ++i) {

                // Free the input() arguments:
                struct QsJob *job = f->jobs + i;

                DASSERT(job->inputBuffers);
                DASSERT(job->inputLens);
                DASSERT(job->isFlushing);
                DASSERT(job->advanceLens);
#ifdef DEBUG
                memset(job->inputBuffers, 0,
                        f->numInputs*sizeof(*job->inputBuffers));
                memset(job->inputLens, 0,
                        f->numInputs*sizeof(*job->inputLens));
                memset(job->isFlushing, 0,
                        f->numInputs*sizeof(*job->isFlushing));
                memset(job->advanceLens, 0,
                        f->numInputs*sizeof(*job->advanceLens));
#endif
                free(job->inputBuffers);
                free(job->inputLens);
                free(job->isFlushing);
                free(job->advanceLens);
            }

            DASSERT(f->readers);
#ifdef DEBUG
            memset(f->readers, 0, f->numInputs*sizeof(*f->readers));
#endif
            free(f->readers);
        }


        if(f->numOutputs) {
            for(uint32_t i=0; i<numJobs; ++i) {

                // Free the input() arguments:
                struct QsJob *job = f->jobs + i;

                DASSERT(job->outputLens);
#ifdef DEBUG
                memset(job->outputLens, 0,
                        f->numOutputs*sizeof(*job->outputLens));
#endif
                free(job->outputLens);
            }
        }


#ifdef DEBUG
        memset(f->jobs, 0, numJobs*sizeof(*f->jobs));
#endif
        free(f->jobs);

        f->jobs = 0;
        f->stage = 0;
        f->unused = 0;
        f->numWorkingThreads = 0;
        f->workingFirst = 0;
        f->workingLast = 0;
    }

    if(f->mutex) {
        DASSERT(f->maxThreads > 1);
        DASSERT(f->stream->maxThreads > 1);
        CHECK(pthread_mutex_destroy(f->mutex));
#ifdef DEBUG
        memset(f->mutex, 0, sizeof(*f->mutex));
#endif
        free(f->mutex);
    }
#ifdef DEBUG
    else {
        DASSERT(f->stream);
        DASSERT(f->maxThreads == 1 || f->stream->maxThreads < 2);
    }
#endif

    if(f->numOutputs) {
        DASSERT(f->outputs);

        // We do not free buffers if they have not been allocated;
        // as in a qsStreamReady() failure case.
        if(f->outputs->buffer)
            FreeBuffers(f);


        // For every output in this filter we free the readers.
        for(uint32_t i=f->numOutputs-1; i!=-1; --i) {
            struct QsOutput *output = &f->outputs[i];
            DASSERT(output->numReaders);
            DASSERT(output->readers);
#ifdef DEBUG
            memset(output->readers, 0,
                    sizeof(*output->readers)*output->numReaders);
#endif
            free(output->readers);
        }

#ifdef DEBUG
        memset(f->outputs, 0, sizeof(*f->outputs)*f->numOutputs);
#endif
        free(f->outputs);
#ifdef DEBUG
        f->outputs = 0;
#endif
        f->numOutputs = 0;
        f->isSource = false;
    }

    f->numInputs = 0;
}


// Note this is call FreerRunResources; it does not free up all stream
// resources, just things that could change between stop() and start().
//
static inline
void FreeRunResources(struct QsStream *s) {

    DASSERT(s);
    DASSERT(s->app);

    for(int32_t i=0; i<s->numConnections; ++i) {
        // These can handle being called more than once per filter.
        FreeFilterRunResources(s->connections[i].from);
        FreeFilterRunResources(s->connections[i].to);
    }

    if(s->maxThreads) {

        CHECK(pthread_mutex_destroy(&s->mutex));
        CHECK(pthread_cond_destroy(&s->cond));
        CHECK(pthread_cond_destroy(&s->masterCond));
#ifdef DEBUG
        memset(&s->mutex, 0, sizeof(s->mutex));
        memset(&s->cond, 0, sizeof(s->cond));
        memset(&s->masterCond, 0, sizeof(s->masterCond));
#endif
        s->maxThreads = 0;
        s->numThreads = 0;
        s->numIdleThreads = 0;
        s->jobFirst = 0;
        s->jobLast = 0;
    }

    if(s->numSources) {
        // Free the stream sources list
#ifdef DEBUG
        memset(s->sources, 0, sizeof(*s->sources)*s->numSources);
#endif
        free(s->sources);
#ifdef DEBUG
        s->sources = 0;
#endif
        s->numSources = 0;
    }
}


void qsStreamDestroy(struct QsStream *s) {

    DASSERT(_qsMainThread == pthread_self(), "Not main thread");
    DASSERT(s);
    DASSERT(s->app);
    DASSERT(s->numConnections || s->connections == 0);
    DASSERT(!(s->flags & _QS_STREAM_START), "We are starting");
    DASSERT(!(s->flags & _QS_STREAM_STOP), "We are stopping");

    FreeRunResources(s);

    // Remove this stream from all the filters in the listed connections.
    for(uint32_t i=0; i<s->numConnections; ++i) {
        DASSERT(s->connections[i].from);
        DASSERT(s->connections[i].to);
        DASSERT(s->connections[i].from->stream == s ||
                s->connections[i].from->stream == 0);
        DASSERT(s->connections[i].to->stream == s ||
                s->connections[i].to->stream == 0);

        s->connections[i].from->stream = 0;
        s->connections[i].to->stream = 0;
    }

    // Find and remove this stream from the app.
    //
    struct QsStream *S = s->app->streams;
    struct QsStream *prev = 0;
    while(S) {
        if(S == s) {

            if(prev)
                prev->next = s->next;
            else
                s->app->streams = s->next;

            CleanupStream(s);
            break;
        }
        prev = S;
        S = S->next;
    }

    DASSERT(S, "stream was not found in the app object");
}



static uint32_t CountFilterPath(struct QsStream *s,
        struct QsFilter *f, uint32_t loopCount, uint32_t maxCount) {

//DSPEW("filter \"%s\" loopCount=%" PRIu32 " maxCount=%"
//          PRIu32, f->name, loopCount, maxCount);

    if(loopCount > maxCount)
        // break the looping.  Stop recursing.  We must have already
        // started looping, because we counted more filters in the path
        // than the number of filters that exist.
        return loopCount;

    ++loopCount;

    uint32_t returnCount = loopCount;


    // In any path in the flow we can only have a filter traversed once.
    // If in any path a filter is traversed more than once then there will
    // be loops and if there is a loop in any part this will recurse
    // forever.
    //
    // Easier is to check if there is a filter path greater than
    // the number of total filters that can be traversed (maxCount).
    //
    for(uint32_t i=0; i<s->numConnections; ++i)
        // look for this source as a "from" filter
        if(s->connections[i].from == f) {
            uint32_t count =
                // recurse
                CountFilterPath(s, s->connections[i].to,
                        loopCount, maxCount);
            // this filter feeds the "to" filter.
            if(count > returnCount)
                // We want the largest filter count in all paths.
                returnCount = count;
        }

//DSPEW("filter \"%s\"  returnCount=%" PRIu32, f->name, returnCount);

    return returnCount;
}



// Allocate the array filter->outputs and filter->outputs[].reader, and
// recure to all filters in the stream (s).
//
static bool
AllocateFilterOutputsFrom(struct QsStream *s, struct QsFilter *f,
        bool ret) {

    // This will work if there are loops in the graph.

    DASSERT(f->numOutputs == 0);
    DASSERT(f->numInputs == 0);

    f->mark = false; // Mark the filter output as dealt with.

    DASSERT(f->outputs == 0);

    // Count the number of filters that this filter connects to with a
    // different output port number.
    uint32_t i;
    for(i=0; i<s->numConnections; ++i) {
        if(s->connections[i].from == f) {
            // There can be many connections to one output port.  We
            // require that output port numbers be in a sequence, but some
            // output ports feed more than one filter.  We are not dealing
            // with input port numbers at this time.
            if(s->connections[i].fromPortNum == f->numOutputs)
                ++f->numOutputs;
            else if(s->connections[i].fromPortNum == QS_NEXTPORT)
                ++f->numOutputs;
            else if(s->connections[i].fromPortNum < f->numOutputs)
                continue;
            else {
                // This is a bad output port number so we just do this
                // default action instead of adding a failure mode, except
                // in DEBUG we'll assert.
                DASSERT(false, "filter \"%s\" has output port number %"
                        PRIu32
                        " out of sequence; setting it to %" PRIu32,
                        s->connections[i].from->name,
                        s->connections[i].fromPortNum,
                        f->numOutputs);
                ERROR("filter \"%s\" has output port number %" PRIu32
                        " out of sequence; setting it to %" PRIu32,
                        s->connections[i].from->name,
                        s->connections[i].fromPortNum,
                        f->numOutputs);

                s->connections[i].fromPortNum = f->numOutputs++;
            }
        }
    }


    if(f->numOutputs == 0)
        // There are no outputs.
        return ret;

    // Make the output array
    ASSERT(f->numOutputs <= _QS_MAX_CHANNELS,
            "%" PRIu32 " > %" PRIu32 " outputs",
            f->numOutputs, _QS_MAX_CHANNELS);

    f->outputs = calloc(f->numOutputs, sizeof(*f->outputs));
    ASSERT(f->outputs, "calloc(%" PRIu32 ",%zu) failed",
            f->numOutputs, sizeof(*f->outputs));

    // Now setup the readers array in each output
    //
    for(uint32_t outputPortNum = 0; outputPortNum<f->numOutputs;
            ++outputPortNum) {

        f->outputs[outputPortNum].maxWrite = QS_DEFAULTMAXWRITE;

        // Count the number of readers for this output port
        // (outputPortNum):
        uint32_t numReaders = 0;
        for(i=0; i<s->numConnections; ++i)
            if(s->connections[i].from == f && (
                    s->connections[i].fromPortNum == outputPortNum ||
                    s->connections[i].fromPortNum == QS_NEXTPORT)
              )
                ++numReaders;

        DASSERT(numReaders);
        DASSERT(_QS_MAX_CHANNELS >= numReaders);

        struct QsReader *readers =
            calloc(numReaders, sizeof(*readers));
        ASSERT(readers, "calloc(%" PRIu32 ",%zu) failed",
                numReaders, sizeof(*readers));
        f->outputs[outputPortNum].readers = readers;
        f->outputs[outputPortNum].numReaders = numReaders;

        // Now set the values in reader:
        uint32_t readerIndex = 0;
        for(i=0; i<s->numConnections; ++i)
            if(s->connections[i].from == f && (
                    s->connections[i].fromPortNum == outputPortNum ||
                    s->connections[i].fromPortNum == QS_NEXTPORT)
                    ) {
                struct QsReader *reader = readers + readerIndex;
                reader->filter = s->connections[i].to;
                reader->threshold = QS_DEFAULTTHRESHOLD;
                reader->maxRead = QS_DEFAULTMAXREADPROMISE;

                // We'll set the reader->inputPortNum later in
                // SetupInputPorts(), if inputPortNum is QS_NEXTPORT.
                // We don't now because we are not counting input filters
                // now.
                //
                reader->inputPortNum = s->connections[i].toPortNum;
                ++readerIndex;
            }

        DASSERT(readerIndex == numReaders);
    }


    // Now recurse if we need to.
    //
    for(i=0; i<f->numOutputs; ++i) {
        uint32_t numReaders = f->outputs[i].numReaders;
        for(uint32_t j=0; j<numReaders; ++j) {
            struct QsFilter *filter = f->outputs[i].readers[j].filter;
            DASSERT(filter);
            if(filter->mark)
                ret = AllocateFilterOutputsFrom(s, filter, ret);
        }
    }

    return ret;
}


// ret is a return value passed back from recusing.
//
// This happens after AllocateFilterOutputsFrom() above.
//
static bool
SetupInputPorts(struct QsStream *s, struct QsFilter *f, bool ret) {

    f->mark = false; // filter done

    DASSERT(f->numInputs == 0, "");


#define MARKER  ((uint8_t *)1)

    // For every filter that has output:
    for(uint32_t i=0; i<s->numConnections; ++i) {
        struct QsFilter *outFilter = s->connections[i].from;
        DASSERT(outFilter);
        // Every output
        for(uint32_t j=0; j<outFilter->numOutputs; ++j) {
            struct QsOutput *output = &outFilter->outputs[j];
            DASSERT(output);
            DASSERT(output->numReaders);
            DASSERT(output->readers);
            for(uint32_t k=0; k<output->numReaders; ++k) {
                // Every reader
                struct QsReader *reader = output->readers + k;
                if(reader->filter == f && reader->readPtr == 0) {
                    ++f->numInputs;
                    // We borrow this readPtr variable to mark this reader
                    // as counted.
                    reader->readPtr = MARKER;
                }
            }
        }
    }


    DASSERT(f->numInputs <= _QS_MAX_CHANNELS);

    // Now check the input port numbers.

    uint32_t inputPortNums[f->numInputs];
    uint32_t inputPortNum = 0;
    memset(inputPortNums, 0, sizeof(inputPortNums));

    // For every filter that has output:
    for(uint32_t i=0; i<s->numConnections; ++i) {
        struct QsFilter *outFilter = s->connections[i].from;
        // Every output
        for(uint32_t j=0; j<outFilter->numOutputs; ++j) {
            struct QsOutput *output = &outFilter->outputs[j];
            for(uint32_t k=0; k<output->numReaders; ++k) {
                // Every reader
                struct QsReader *reader = output->readers + k;
                // We borrow this readPtr variable again to mark this reader
                // as not counted; undoing the setting of readPtr.
                if(reader->filter == f && reader->readPtr == MARKER) {
                    // Undo the setting of readPtr.
                    reader->readPtr = 0;

                    if(reader->inputPortNum == QS_NEXTPORT)
                        reader->inputPortNum = inputPortNum;
                    if(reader->inputPortNum < f->numInputs)
                        // Mark inputPortNums[] as gotten.
                        inputPortNums[reader->inputPortNum] = 1;
                    //else:  We have a bad input port number.

                    ++inputPortNum;
                }
            }
        }
    }

    DASSERT(inputPortNum == f->numInputs);


    // Check that we have all input port numbers, 0 to N-1, being written
    // to.
    for(uint32_t i=0; i<f->numInputs; ++i)
        if(inputPortNums[i] != 1) {
            ret = true;
            WARN("filter \"%s\" has some bad input port numbers",
                    f->name);
            break; // nope
        }


    if(f->numInputs) {
        // Allocate and set the
        // filter->readers[inputPort] = output feeding reader
        //
        f->readers = calloc(f->numInputs, sizeof(*f->readers));
        ASSERT(f->readers, "calloc(%" PRIu32 ",%zu) failed",
                f->numInputs, sizeof(*f->readers));

        // We got to look at all filter outputs in the stream to find the
        // input readers for this filter, f.
        for(uint32_t i=0; i<s->numConnections; ++i)
            // This i loop may repeat some filters, but that may be better
            // than adding more data structures to the stream struct.
            if(s->connections[i].to == f) {
                struct QsOutput *outputs = s->connections[i].from->outputs;
                uint32_t numOutputs = s->connections[i].from->numOutputs;
                for(uint32_t j=0; j<numOutputs; ++j) {
                    struct QsReader *readers = outputs[j].readers;
                    uint32_t numReaders = outputs[j].numReaders;
                    for(uint32_t k=0; k<numReaders; ++k)
                        if(readers[k].filter == f) {
                            uint32_t inputPortNum = readers[k].inputPortNum;
                            DASSERT(inputPortNum < f->numInputs);
                            // It should only have one reader from one
                            // output, so:
                            if(f->readers[inputPortNum] == 0)
                                // and we set it once here for all inputs
                                // found:
                                f->readers[inputPortNum] = readers + k;
                            else {
                                DASSERT(f->readers[inputPortNum] == readers + k);
                                // We have already looked at this filter and
                                // output.  We are using the stream
                                // connection list which can have filters
                                // listed more than once.
                                j = numOutputs; // pop out of j loop
                                break; // pop out of this k loop
                            }
                        }
                }
            }
    }

#ifdef DEBUG
    for(uint32_t i=0; i<f->numInputs; ++i)
        DASSERT(f->readers[i]);
#endif

    // Now recurse to filters that read from this filter if we have not
    // already.
    for(uint32_t i=0; i<f->numOutputs; ++i) {
        struct QsOutput *output = &f->outputs[i];
        for(uint32_t j=0; j<output->numReaders; ++j) {
            // Every reader
            struct QsReader *reader = output->readers + j;
            if(reader->filter->mark)
                // Recurse
                ret = SetupInputPorts(s, reader->filter, ret);
        }
    }

    return ret;
}


int qsStreamStop(struct QsStream *s) {

    DASSERT(_qsMainThread == pthread_self(), "Not main thread");
    DASSERT(s);
    DASSERT(s->app);
    DASSERT(!(s->flags & _QS_STREAM_START), "We are starting");
    DASSERT(!(s->flags & _QS_STREAM_STOP), "We are stopping");
    ASSERT(pthread_getspecific(_qsKey) == 0,
            "stream is in the wrong mode to call this now.");
    ASSERT(s->sources != 0,
            "stream is in the wrong mode to call this now.");


    s->flags &= (~_QS_STREAM_LAUNCHED);

    if(!s->sources) {
        // The setup of the stream failed and the user ignored it.
        WARN("The stream is not setup");
        return -1; // failure
    }


    /**********************************************************************
     *     Stage: call all stream's filter stop() if present
     *********************************************************************/

    for(struct QsFilter *f = s->app->filters; f; f = f->next)
        if(f->stream == s && f->stop) {
            CHECK(pthread_setspecific(_qsKey, f));
            f->mark = _QS_IN_STOP;
            s->flags |= _QS_STREAM_STOP;
            f->stop(f->numInputs, f->numOutputs);
            s->flags &= ~_QS_STREAM_STOP;
            f->mark = 0;
            CHECK(pthread_setspecific(_qsKey, 0));
        }


    /**********************************************************************
     *     Stage: cleanup
     *********************************************************************/

    FreeRunResources(s);


    s->flow = 0;


    return 0; // success
}



int qsStreamReady(struct QsStream *s) {

    DASSERT(_qsMainThread == pthread_self(), "Not main thread");
    DASSERT(s);
    DASSERT(s->app);
    DASSERT(!(s->flags & _QS_STREAM_START), "We are starting");
    DASSERT(!(s->flags & _QS_STREAM_STOP), "We are stopping");
    ASSERT(pthread_getspecific(_qsKey) == 0,
            "The stream is in the wrong mode to call this now.");
    ASSERT(s->sources == 0,
            "The stream is in the wrong mode to call this now.");


    ///////////////////////////////////////////////////////////////////////
    //                                                                   //
    //  This has a few stages in which we go through the lists, check    //
    //  things out and set things up.                                    //
    //                                                                   // 
    ///////////////////////////////////////////////////////////////////////

    // Failure error return values start at -1 and go down from there;
    // like: -1, -2, -3, -4, ...

    /**********************************************************************
     *      Stage: lazy cleanup from last run??
     *********************************************************************/

    //FreeRunResources(s);


    /**********************************************************************
     *      Stage: Find source filters
     *********************************************************************/

    DASSERT(s->numSources == 0);

    for(uint32_t i=0; i < s->numConnections; ++i) {

        struct QsFilter *f = s->connections[i].from;
        uint32_t j=0;
        for(; j<s->numConnections; ++j) {
            if(s->connections[j].to == f)
                // f is not a source
                break;
        }
        if(!(j < s->numConnections) && !f->isSource) {
            // Count the sources.
            ++s->numSources;
            f->isSource = true;
        }
    }

 
    if(!s->numSources) {
        // It's not going to flow, or call any filter callbacks
        // because there are no sources.
        ERROR("This stream has no sources");
        // We have nothing to free at this time so we just return with
        // error.
        return -1; // error no sources
    }

    s->sources = calloc(1, s->numSources*sizeof(*s->sources));
    ASSERT(s->sources, "calloc(1,%zu) failed", 
            s->numSources*sizeof(*s->sources));

    // Get a pointer to the source filters.
    uint32_t j = 0;
    for(uint32_t i=0; i < s->numConnections; ++i)
        if(s->connections[i].from->isSource) {
            uint32_t k=0;
            for(;k<j; ++k)
                if(s->sources[k] == s->connections[i].from)
                    break;
            if(k == j)
                // s->connections[i].from was not in the s->sources[] so
                // add it.
                s->sources[j++] = s->connections[i].from;
        }

    DASSERT(j == s->numSources);


    /**********************************************************************
     *      Stage: Check flows for loops, if we can't have them
     *********************************************************************/

    if(!(s->flags && _QS_STREAM_ALLOWLOOPS))
        // In this case, we do not allow loops in the filter graph.
        for(uint32_t i=0; i<s->numSources; ++i)
            if(CountFilterPath(s, s->sources[i], 0, s->numConnections) >
                    s->numConnections + 1) {
                ERROR("stream has loops in it consider"
                        " calling: qsStreamAllowLoops()");
//#ifdef DEBUG
                qsAppDisplayFlowImage(s->app, QSPrintOutline, true);
//#endif
                FreeRunResources(s);
                return -2; // error we have loops
            }


    /**********************************************************************
     *      Stage: Set up filter output and output::readers
     *********************************************************************/

    StreamSetFilterMarks(s, true);
    for(uint32_t i=0; i<s->numSources; ++i)
        if(AllocateFilterOutputsFrom(s, s->sources[i], false)) {
//#ifdef DEBUG
            qsAppDisplayFlowImage(s->app, 0, true);
//#endif
            FreeRunResources(s);
            return -3; // error we have loops
        }


    /**********************************************************************
     *      Stage: Set up filter input ports
     *********************************************************************/

    StreamSetFilterMarks(s, true);
    for(uint32_t i=0; i<s->numSources; ++i)
        if(SetupInputPorts(s, s->sources[i], false)) {
            ERROR("stream has bad input port numbers");
//#ifdef DEBUG
            qsAppDisplayFlowImage(s->app, QSPrintDebug, true);
//#endif
            FreeRunResources(s);
            return -4; // error we have loops
        }


    /**********************************************************************
     *      Stage: call all stream's filter start() if present
     *********************************************************************/

    // By using the app list of filters we do not call any filter start()
    // more than once, (TODO) but is the order in which we call them okay?
    //
    for(struct QsFilter *f = s->app->filters; f; f = f->next) {
        if(f->stream == s) {
            if(f->start) {
                // We mark which filter we are calling the start() for so
                // that if the filter start() calls any filter API
                // function to get resources we know what filter these
                // resources belong to.
                CHECK(pthread_setspecific(_qsKey, f));
                f->mark = _QS_IN_START;
                s->flags |= _QS_STREAM_START;
                // Call a filter start() function:
                int ret = f->start(f->numInputs, f->numOutputs);
                s->flags &= ~_QS_STREAM_START;
                f->mark = 0;
                CHECK(pthread_setspecific(_qsKey, 0));

                if(ret) {
                    // TODO: Should we call filter stop() functions?
                    //
                    ERROR("filter \"%s\" start()=%d failed", f->name, ret);
                    return -3; // We're screwed.
                }
            }
        }
    }


    /**********************************************************************
     *     Stage: allocate output buffer structures
     *********************************************************************/

    // Any filters' special buffer requirements should have been gotten
    // from the filter's start() function.  Now we allocate any output
    // buffers that have not been explicitly allocated from the filter
    // start() calling qsCreateOutputBuffer().
    //
    StreamSetFilterMarks(s, true);
    for(uint32_t i=0; i<s->numSources; ++i)
        AllocateBuffer(s->sources[i]);


    /**********************************************************************
     *     Stage: mmap() ring buffers to memory
     *********************************************************************/

    // There may be some calculations needed from the buffer structure for
    // the memory mapping, so we do this in a different loop.
    StreamSetFilterMarks(s, true);
    for(uint32_t i=0; i<s->numSources; ++i)
        MapRingBuffers(s->sources[i]);



    return 0; // success
}
