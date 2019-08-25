#ifndef __qsapp_h__
#define __qsapp_h__

#include <inttypes.h>
#include <stdbool.h>

/** \file
 *
 * The libqsapp application programming interface (API)
 *
 * You can link your C and C++ code with the libqsapp library to make
 * quickstream programs that load quickstream filter modules, assemble
 * these filters into flow graphs, and run the flow graphs.
 *
 * You can do the same with the program \ref quickstream.
 *
 *
 * The following terms are used in function names:
 *
 * API objects
 *
 * - **Qs app**     a collection of loaded filter module plugins,
 *                  a collection of streams
 *                  an application container for all that is quickstream
 *
 * - **Qs stream**  a directed graph of filters,
 *                  a mapping of filters to threads,
 *                  a mapping of threads to processes
 *
 * - **Qs filter**  a filter module plugin from a dynamic shared object
 *                  file
 *
 * - **Qs process** an operating system process
 *
 * - **Qs thread**  an operating system thread (pthread)
 *
 */ 


/** quickstream release version number
 *
 * This release version number is not necessarily the same as library
 * version numbers.
 */
// Change this QS_VERSION macro string to make a new version.
#define QS_VERSION "0.0.1"

/** Read the last quickstream error string
 *
 * The user must free() any non-zero returned pointer.  This string is
 * allocated in order to make this thread safe.
 *
 * Calling this function also clears the current error.  This interface is
 * defective, in that new errors in other threads may overwrite the
 * internal error string before it can be read with this, or by any
 * other thread.  Per-thread error strings create other issues.
 *
 * To clear the current error, if there is one or not, call
 * free(qsError()).  This can be called without checking if there is an
 * error or not.  free(0) is valid code.  See `man 3 free`.
 *
 * \return 0 or a malloc() allocated pointer to a string that the user
 * must free().
 *
 *  \todo add error return int values that correspond to strings.
 */
extern
char *qsError(void);


/** Create the highest level quickstream construct,
 * a quickstream app.
 *
 * \return a pointer to an app structure, or 0 on failure*/
extern
struct QsApp *qsAppCreate(void);


extern
int qsAppDestroy(struct QsApp *app);


/** Add a filter module to the list of filter modules to be loaded.
 *
 * \param app that was returned from a call to qsAppCreate().
 * \param fileName that refers to the plugin module file.
 * \param loadName a made-up unquie name that was refer to this
 * loaded plugin module.  loadName=0 maybe passed in to have the
 * name generated based on fileName.
 *
 * \return 0 on success.
 */
extern
struct QsFilter *qsAppFilterLoad(struct QsApp *app,
        const char *fileName,
        const char *loadName);

extern
int qsAppPrintDotToFile(struct QsApp *app, FILE *file);


/**
 *
 *
 * \param waitForDisplay if set this will not return until the
 * display program exits.
 *
 * \return 0 on success.
 */
extern
int qsAppDisplayFlowImage(struct QsApp *app, bool waitForDisplay);


extern
int qsFilterUnload(struct QsFilter *filter);


extern
struct QsStream *qsAppStreamCreate(struct QsApp *app);

extern
int qsStreamDestroy(struct QsStream *stream);

/** Remove a filter and it's connections from a stream
 *
 * This does not unload the filter module plugin.
 *
 * \return 0 if the filter was found and removed, and non-zero otherwise.
 */
extern
int qsStreamRemoveFilter(struct QsStream *stream, struct QsFilter *filter);

extern
int qsStreamConnectFilters(struct QsStream *stream,
        struct QsFilter *from, struct QsFilter *to);

/** Start the flow
 *
 * Launches threads and calls the filter callbacks ... bla bla.
 *
 * When properly run, qsStreamStop() will be called after this.
 *
 * \param stream the stream object that has the filters that are
 * connected.
 *
 * \param autoStop if set call qsStreamStop() before returning.
 *
 * \return 0 if the stream runs and non-zero on error.
 */
extern
int qsStreamStart(struct QsStream *stream);


/** Stop the flow
 *
 * For a given stream qsStreamStop() be called after qsStreamStart().
 *
 */
extern
int qsStreamStop(struct QsStream *stream);



/** Create a thread object
 *
 * This does not start the thread.  It just sets up what threads
 * will be used when the stream runs.  See qsStreamStart() and
 * qsStreamStop().
 *
 * \param stream returned from qsAppStreamCreate().
 *
 * \return a pointer to a thread object, or 0 on failure.
 */
extern
struct QsThread *qsStreamThreadCreate(struct QsStream *stream);


extern
int qsThreadDestroy(struct QsThread *thread);


extern
int qsThreadAddFilter(struct QsThread *thread, struct QsFilter *filter);



#endif // #ifndef __qsapp_h__
