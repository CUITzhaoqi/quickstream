// This is the private data for the filter modules part of libquickstream


// There can be only one thread running one filters input() function, so
// _qsCurrentFilter marks the filter who's input() function is currently
// being called.
//
extern __thread
struct QsFilter *_qsCurrentFilter;

