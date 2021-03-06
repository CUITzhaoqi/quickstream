
// GetJob() is boiler plate code
// (https://en.wikipedia.org/wiki/Boilerplate_code) that is at the top of
// the API functions that can be called from the filter's input()
// function.
static inline
struct QsJob *GetJob(void) {

    struct QsJob *j = pthread_getspecific(_qsKey);
    // If job, j, was not in thread specific data than this could be due
    // to a user calling this function while not in a filter module
    // input() call.
    ASSERT(j, "Not from code in a filter module input() function");
    // Double check with a magic number if DEBUG
    DASSERT(j->magic == _QS_IS_JOB);

#ifdef DEBUG
    struct QsFilter *f = j->filter;
    DASSERT(f);
    struct QsStream *s = f->stream;
    DASSERT(s);
    DASSERT(!(s->flags & _QS_STREAM_START), "Stream is starting");
    DASSERT(!(s->flags & _QS_STREAM_STOP), "Stream is stopping");
    DASSERT(f->mark == 0, "This finished filter \"%s\" "
            "should not be calling input()", f->name);
#endif

    return j;
}


// This is like GetJob() but does not require that this be called from a
// filter input() function, so this may return 0 if we are not in a
// filter input() function.
static inline
struct QsFilter *GetFilter(void) {

    struct QsJob *j = pthread_getspecific(_qsKey);
    // If job, j, was not in thread specific data than this could be due
    // to a user calling this function while not in a filter module
    // input() call.
    if(!j) return 0;

    if(j->magic == _QS_IS_JOB) {
        // This is a job.
        struct QsFilter *f = j->filter;
        DASSERT(f);
        DASSERT(f->stream);
        return f;
    }

    // j is not really a job.

    struct QsFilter *f = (struct QsFilter *) j;
    // f should really be a filter since it's not a job.
    // It's the only possibility since it's not a job;
    // so we just check in the DEBUG case.
    DASSERT(f->mark == _QS_IN_CONSTRUCT ||
         f->mark == _QS_IN_DESTROY ||
         f->mark == _QS_IN_START   ||
         f->mark == _QS_IN_STOP);

    // thread specific data is really a filter.
    DASSERT(f->stream);
    return f;
}
