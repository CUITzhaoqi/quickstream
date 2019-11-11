/* This is a private header file that is only used in the quickstream code
 * internal to libquickstream.so.  The libquickstream.so user, in general,
 * should not include this header file.  Hence this file is not installed
 * and is kept with the lib/libquickstream.so source code.
 */


// This file is the guts of quickstream.  By understanding each data
// member in these data structures you can get a clear understanding of
// the insides of quickstream, and code it.  Easier said than done...


// Then studying these structures one must keep in mind most data is
// constant at stream flow-time, and just some of the data is changing
// at stream flow-time.  When we have multiple threads accessing this
// data we can have only certain threads make changes to certain data,
// and avoid thread synchronization primitives, and other system calls.


// App (QsApp) is the top level quickstream object.  It's a container for
// filters and streams.  Perhaps there should only be one app in a
// program, but we do not impose that.  There is no compelling reason to
// limit the number apps that a program can have.  App is used to create
// filters by loading module plugins.  App is used to create streams.
//
struct QsApp {

    // List of filters.  Head of a singly linked list.
    struct QsFilter *filters;

    // We could have more than one stream, but we can't delete or edit one
    // while it is running.  You could do something weird like configure
    // one stream while another stream is running.
    //
    // List of streams.  Head of a singly linked list.
    struct QsStream *streams;
};


// QsThread is only in the QsStream struct

struct QsThread {

    pthread_t pthread;
    struct QsThread *next;
};


QS_STREAM_DEFAULTMAXTHTREADS         (8)

// bit Flags for the stream
//
// this is a stream configuration option bit flag
#define _QS_STREAM_ALLOWLOOPS        (01)


// This is the value of the stream flag when the stream is first created.
#define _QS_STREAM_DEFAULTFLAGS      (0)

// set if qsStreamLaunch() was called successfully
// unset in qsStreamStop()
//
// this is a stream state bit flag
#define _QS_STREAM_LAUNCHED          (02)

// By limiting  the number of channels possible in or out we can use stack
// allocation via alloca().
#define QS_MAX_CHANNELS              ((uint32_t) (4*1024))



// Stream (QsStream) is the thing the manages a group of filters and their
// flow state.  Since streams can add and remove filters when it is not
// flowing the stream needs app to be a list of loaded filters for it.
//
struct QsStream {

    struct QsApp *app;

    // maxThreads=0 means do not start any.  maxThreads does not change at
    // flow/run time, so we need no mutex to access it.
    uint32_t maxThreads; // Will not create more pthreads than this.

    uint32_t flags; // bit flags that configure the stream
    // example the bit _QS_STREAM_ALLOWLOOPS may be set to allow loops
    // in the graph.  flags does not change at flow/run time, so we need
    // no mutex to access it.


    //////////////////// STREAM MUTEX GROUP ///////////////////////////////
    //
    // These are the only variables in QsStream that change at flow/run
    // time.
    //
    // This mutex protects the reading and writing of things that can
    // change in this stream struct when the stream is flowing.  There is
    // not much time needed to access the little changing data in the
    // stream, relatively speaking, or so we hope.  We expect a write/read
    // lock would not be as good as a simple mutex, given the low
    // probability of inter thread contention on this mutex.
    //
    // TODO: We can add the mutex contention test with
    // pthread_mutex_trylock() when we compile wityh DEBUG.
    //
    pthread_mutex_t mutex;
    //
    uint32_t flowState; // must have a mutex lock to access flowState.
    //
    // number of pthreads that exist from this stream, be they idle or
    // flowing.
    uint32_t numThreads; // must have a mutex lock to access numThreads.
    // number of thread that at in the idle thread stack.
    uint32_t numIdleThreads;
    //
    // We keep a stack/pool of idle threads in idleThreads.  The running
    // threads handle themselves and we do not keep a list of running
    // pthreads.  We must have a mutex lock to access idleThreads.
    struct QsThread *idleThreads;
    //
    ///////////////////////////////////////////////////////////////////////


    // The array list of sources is created at start:
    uint32_t numSources;       // length of sources
    // We also use sources==0 (or source != 0) as a flag that shows that
    // the stream flow-time resources have not been allocated yet (or
    // not), in place of introducing another flag.
    struct QsFilter **sources; // array of filter sources

    // This list of filter connections is not used while the stream is
    // running.  It's queried a stream start, and the QsFilter data
    // structs are setup at startup.  The QsFilter data structures are
    // used when the stream is running.
    //
    // tallied filter connections:
    uint32_t numConnections;// length of "from" and "to" arrays
    struct QsFilter **from; // array of filter pointers
    struct QsFilter **to;   // array of filter pointers

    struct QsStream *next; // next stream in app list of streams
};


struct QsOutput;

// The filter (QsFilter) is loaded by app as a module DSO (dynamic shared
// object) plugin.  The filter after be loaded is added to a only one
// stream at a time.  If the filter is added to another stream, it will be
// removed from the previous stream; so ya, a filter can be only in one
// stream at a time.  When finished with a filter, the filter is unloaded
// by its' app.  quickstream users can write filters using
// include/qsfilter.h.  The quickstream software package also comes with a
// large selection of filters.
//
struct QsFilter {

    void *dlhandle; // from dlopen()

    struct QsApp *app;       // This does not change
    struct QsStream *stream; // This stream can be changed
    struct QsThread *thread; // thread/process that this filter will run in

    // name never changes after filter loading/creation pointer to
    // malloc()ed memory.
    char *name; // unique name for the Filter in a given app

    // Callback functions that may be loaded.  We do not get a copy of
    // the construct() and destroy() functions because they are only
    // called once, so we just dlsym() (if we have a dlhandle) them just
    // before we call them.
    int (* start)(uint32_t numInputs, uint32_t numOutputs);
    int (* stop)(uint32_t numInputs, uint32_t numOutputs);
    int (* input)(void *buffer, size_t len, uint32_t inputChannelNum,
            uint32_t flowState);


    struct QsFilter *next; // next loaded filter in app list

    // Set to true if this filter input() can be called by more than one
    // thread at a time.
    //
    bool isThreadSafe;


    ///////////////////////////////////////////////////////////////////////
    // The following are setup at stream start and cleaned up at stream
    // stop
    ///////////////////////////////////////////////////////////////////////


    bool isSource;  // startup flag marking filter as a source


    // This filter owns these output structs, in that it is the only
    // filter that may change the ring buffer pointers.
    //
    uint32_t numOutputs; // number of connected filter we write to.
    struct QsOutput *outputs; // array of struct QsOutput

    uint32_t numInputs; // number of connected input filters feeding this.


    //////////////////// STREAM MUTEX GROUP ///////////////////////////////
    //
    // These variables are in the STREAM MUTEX GROUP and require the
    // stream mutex lock to access them at flow/run time.
    //
    // TODO: If there is just one filter feeding (outputting to) this
    // filter than no mutex lock will to needed to access this group
    // of data: buffer, len, and flowState; for that case the only filter
    // (thread) that will access this variable group is the feeding
    // filter.
    //
    //
    // We require a list related to outputs from filters writing to this
    // filter, so that we may check that input thresholds are satisfied
    // before calling input().  This is accessed by any filters that are
    // outputting to this filter, so ya we need a mutex lock.
    // The access time should be relatively short, for it's just setting
    // up to pointers to ring buffers
    //
    // Used for setting the parameters for calls to the filters input(),
    // and for checking that thresholds are met before calling input().
    // The buffer pointers and lengths get set by filters that maybe
    // outputting.  If more than one filter is outputting to the filter
    // that this input() corresponds to than a mutex lock is required
    // to read or write these values.  Once the values are copied to
    // and input() args, then they are in that function call stack and
    // the values in this struct are free to be accessed by another
    // filter with the mutex lock.
    //
    void *buffer[];
    // This is not necessarily the same as the length that is passed to
    // input(), but that lengths is calculated from this. 
    size_t len[];
    // flowState comes just from the filters that write to this filter.
    // In effect it's a marker in the stream flow.  We do not want filters
    // that are very far down stream to see flowState changes that have
    // not propagated to them yet.
    uint32_t flowState;
    //
    ///////////////////////////////////////////////////////////////////////



    // TODO: It'd be nice not to have this extra data. It's not needed
    // when the stream is flowing.
    //
    // mark is extra data in this struct so that we can save a marker
    // as we look through the filters in the graph, because if the graph
    // has cycles in it, it's not easy to look at each filter just once
    // without a marker to mark a filter as looked at.
    bool mark;
};



struct QsOutput {  // points to reader filters

    // readPtr and writer->writePtr are the two only values in this struct
    // that can change while the stream is in the flowing state.

    // The "reading filter" (or access filter) that sees this output as
    // input.
    struct QsFilter *filter;

    // The input channel number that the filter being written to sees
    // in it's input(,,channelNum,) call.
    uint32_t inputChannelNum;


    // Here's where it gets weird: We need a buffer and a write pointer,
    // where the buffer may be shared between outputs in the same filter
    // and shared between outputs in different adjacent filters.
    // So we may have more than one output point to a given writer.
    //
    // The writer for this particular output.  This writer can be shared
    // between many outputs in one filter, hence this is just a pointer
    // and a single writer that may be used for more than one output.
    //
    // TODO: "pass-through buffers" in which the buffer can be shared
    // for multiple filter levels, that is a filter can read the buffer
    // and then treat it like it is the writer to the next filter.
    // The passing filter can't change the size of the buffer, but it can
    // over-write it's content if it's careful.  If a filter at the same
    // level does this then the ring buffer memory will not necessarily
    // have what you want in its' memory.
    //
    // ** Only the filter (and it's thread) that has a pointer to this
    // output can read and write to the writePtr in this writer, at flow
    // time.
    // **
    // 
    struct QsWriter *writer;


    // read filter access pointer to the circular buffer.
    //
    // ** Only the filter (and it's thread) that has a pointer to this
    // output can read and write this pointer, at flow time. **
    //
    uint8_t *readPtr;


    // All these limits may be set in the reading filters start()
    // function.
    //
    // Sizes in bytes that may be set at filter start() and
    // do not change at flow time:
    size_t

        inputThreshold, // This reading filter will not read (input)
        // any data until this threshold is met; so we will not call the
        // filter input() function unless this threshold is met.
        // inputThreshold is the input length needed to trigger an input()
        // call.  When the flow is flushing this is ignored.

        maxInput; // This reading filter will not read (input) more than
        // this, if this is set.  The filter sets this so that the stream
        // running does not call input() with more data than this.  This
        // is a convenience, so the filter does not need to tell the
        // stream running to not advance the buffer so far at every
        // input() call had the input buffer length exceeded this number.
        // When there is more readable data than this, the read filter
        // input() will be called many times, with buffer read length
        // values of maxInput.
};


struct QsWriter {  // all outputs need a writer

    // There can be many outputs pointing to each writer.

    size_t maxWrite; // maximum the writer can write.  If it writes more
    // than this then the memory could be overrun.

    // QsBuffer may be shared by other QsWriter's, if the buffer is shared
    // it's a pass-through buffer, and it should be also written to in an
    // neighboring down-stream filter.
    struct QsBuffer *buffer;

    // This is the last place a writing filter wrote to in this memory.
    uint8_t *writePtr;

    uint32_t refCount; // Number of outputs pointing to this.
};


struct QsBuffer {  // all writers need a circular buffer

    // Their can be many writers pointing to this buffer.
    //
    // If there are writers from different and connected filters then this
    // will be a buffer the passes through one of the filters; a
    // pass-through buffer.

    uint8_t *mem; // Pointer to start of mmap()ed memory.

    // These two parameters make it a circular buffer or ring buffer.  See
    // makeRingBuffer.c.
    size_t mapLength, overhangLength;

    uint32_t refCount; // Number of writers pointing to this.
};



//
// TODO: If an app makes a lot of threads that are not related to
// quickstream, this could be wasteful, so we may want to allocate this.
// Maybe these can be part of QsThread.
//
//
// The current thread that corresponds with the filter who's input() is
// being called now.
extern
__thread struct QsInput {

    size_t len;
    bool advanceInput_wasCalled;
    struct QsFilter *filter; // filter module having input() called.
    uint32_t flowState;

} _input;



// The filter that is having construct(), start(), stop() or destroy()
// called.  There is only one thread when they are called.  TODO: This
// makes the API not thread-safe.
extern
struct QsFilter *_qsCurrentFilter;



// These below functions are not API user interfaces:


extern
void AllocateRingBuffers(struct QsFilter *f);


extern
void FreeRingBuffersAndWriters(struct QsFilter *f);

extern
int stream_run_0p_0t(struct QsStream *s);


extern
void *makeRingBuffer(size_t *len, size_t *overhang);


extern
void freeRingBuffer(void *x, size_t len, size_t overhang);


extern
void setupSendOutput(struct QsFilter *f);


extern
void unsetupSendOutput(struct QsFilter *f);


extern
void _qsOutput(size_t len, uint32_t outputChannelNum);


extern
size_t GetReadLength(struct QsOutput *output);


#if 0 // Not needed yet.
// Set filter->mark = val for every filter in the app.
static inline
void AppSetFilterMarks(struct QsApp *app, bool val) {
    for(struct QsFilter *f=app->filters; f; f=f->next)
        f->mark = val;
}
#endif
// Set filter->mark = val for every filter in just this stream.
static inline
void StreamSetFilterMarks(struct QsStream *s, bool val) {

    if(s->numSources) {
        // Case stream has data structures build for running the flow.
        for(uint32_t i=0; i<s->numSources; ++i)
            s->sources[i]->mark = val;
        for(uint32_t i=0; i<s->numConnections; ++i)
            s->to[i]->mark = val;
        return;
    }

    // Case where the stream is not setup to flow yet, so we have not
    // found the source filters (sources) yet.  The filters are just
    // listed in the stream connection arrays, to[], and from[].
    for(uint32_t i=0; i<s->numConnections; ++i) {
        s->to[i]->mark = val;
        s->from[i]->mark = val;
    }
}
