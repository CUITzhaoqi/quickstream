#ifndef __qsapp_h__
#  error "app.h needs to be included before this file."
#endif

#ifndef __debug_h__
#  error "debug.h needs to be included before this file."
#endif



static inline
struct QsFilter *FindFilterNamed(struct QsApp *app, const char *name) {

    struct QsFilter *F = app->filters;
    // TODO: This could be made quicker, but the quickstream is
    // not in "run" mode now so speed it not really needed now.
    //
    for(F = app->filters; F; F = F->next) {
        if(!strcmp(F->name, name))
            return F; // found
    }

    return 0; // not found
}


// Here is where we cleanup filter (QsFilter) data that is from loading
// and not part of flow-time and stream resources.  Flow-time and stream
// resources are like ring buffers and connections.
static inline
void FreeFilter(struct QsFilter *f) {

    DASSERT(f);
    DASSERT(f->name);
    DASSERT(f->outputs == 0);
    DASSERT(f->numOutputs == 0);
    DASSERT(f->app);

    DSPEW("Freeing: %s", f->name);

    if(f->dlhandle) {
        int (* destroy)(void) = dlsym(f->dlhandle, "destroy");
        if(destroy) {

            struct QsApp *oldApp = pthread_getspecific(_qsKey);
            // Check if the user is using more than one QsApp in a single
            // thread and calling qsApp or qsStream functions from within
            // other qsApp or qsStream functions.
            //
            // You may be able to create more than one QsApp but:
            //
            ASSERT(!oldApp || oldApp == f->app,
                    "You cannot mix QsApp objects in "
                    "other QsApp function calls");

            DASSERT(f->mark == 0);

            if(!oldApp)
                // If thread specific data was set already than we do not
                // set it again here:
                CHECK(pthread_setspecific(_qsKey, f->app));

            // This FreeFilter() function must be re-entrant.  It may be
            // called from a qsFilterUnload() call in a filter->destroy()
            // call; for filters that are super-modules.  Super-modules
            // are filters that can load and unload other filters.
            //
            // However, filters cannot unload themselves.
            //
            // So ya, this code block looks like stupid variable tossing
            // shit, but it's not.
            //
            struct QsFilter *old_qsCurrentFilter = f->app->currentFilter;
            // A filter cannot unload itself.
            DASSERT(old_qsCurrentFilter != f);
            // Use the mark variable at a state marker.
            f->mark = _QS_IN_DESTROY;

            int ret = destroy();

            // Filter f is not in destroy() phase anymore.
            f->mark = 0;
            f->app->currentFilter = old_qsCurrentFilter;

            if(old_qsCurrentFilter == 0)
                // We are the last in this filter unload call stack.
                CHECK(pthread_setspecific(_qsKey, 0));


            if(ret) {
                // TODO: what do we use this return value for??
                //
                // Maybe just to print this warning.
                WARN("filter \"%s\" destroy() returned %d", f->name, ret);
            }
        }

        dlerror(); // clear error
        if(dlclose(f->dlhandle))
            WARN("dlclose(%p): %s", f->dlhandle, dlerror());
            // TODO: So what can I do.

    }


#ifdef DEBUG
    memset(f->name, 0, strlen(f->name));
#endif
    free(f->name);
    memset(f, 0, sizeof(*f));
    free(f);
}


static inline
struct QsFilter *FindFilter_viaHandle(struct QsApp *app, void *handle) {
    DASSERT(app);
    DASSERT(handle);

    for(struct QsFilter *f = app->filters; f; f = f->next)
        if(f->dlhandle == handle)
            return f;
    return 0;
}


//
// This is basically the guts of the filter destructor
//
// Free the filter memory, dlcose() the handle, and remove it from the
// list.
//
static inline
void DestroyFilter(struct QsApp *app, struct QsFilter *f) {

    DASSERT(app);
    DASSERT(app->filters);
    DASSERT(f);

    // Remove any stream filter connections that may include this filter.
    if(f->stream)
        // This filter should be listed in this stream and only this
        // stream.
        qsStreamRemoveFilter(f->stream, f);

    // Remove it from the app list.
    struct QsFilter *F = app->filters;
    struct QsFilter *prev = 0;
    while(F) {
        if(F == f) {
            if(prev)
                prev->next = F->next;
            else
                app->filters = F->next;

            FreeFilter(f);
            break;
        }
        prev = F;
        F = F->next;
    }
    DASSERT(F, "Filter was not found in app list");
}



// name must be unique for all filters in app
//
// TODO: this is not required to be fast; yet.
//
static inline
struct QsFilter *AllocAndAddToFilterList(struct QsApp *app,
        const char *name) {

    DASSERT(app);
    DASSERT(name);
    DASSERT(name[0]);
    struct QsFilter *f = calloc(1, sizeof(*f));
    ASSERT(f, "calloc(1, %zu) failed", sizeof(*f));

    // Check for unique name for this loaded module filter.
    //
    if(FindFilterNamed(app, name)) {
        uint32_t count = 2;
        size_t sLen = strlen(name) + 7;
        f->name = malloc(sLen);

        while(count < 1000000) {
            snprintf(f->name, sLen, "%s-%" PRIu32, name, count);
            ++count;
            if(!FindFilterNamed(app, f->name))
                break;
        }
        // I can't imagine that there will be ~ 1000000 filters.
        DASSERT(count < 1000000);
    } else
        f->name = strdup(name);


    struct QsFilter *fIt = app->filters; // dummy iterator.
    if(!fIt)
        // This is the first filter in this app.
        return (app->filters = f);

    while(fIt->next) fIt = fIt->next;

    // Put it last in the app filter list.
    fIt->next = f;

    return f;
}
