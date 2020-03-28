
struct QsDictionary;


// This Dictionary class is filled with key/value pairs and then built.
// After it is built no more key/value pairs are Inserted.

extern
struct QsDictionary *qsDictionaryCreate(void);


extern
void qsDictionaryDestroy(struct QsDictionary *dict);


// Insert key/value if not present.
// 
// Returns 0 on success or 1 if already present and -1 if it is not added
// and have an invalid character.
extern
int qsDictionaryInsert(struct QsDictionary *dict,
        const char *key, const void *value);


// This is the fast Find() function.
//
// Returns element value for key or 0 if not found.
extern
void *qsDictionaryFind(const struct QsDictionary *dict, const char *key);


// Subtree Searching
//
// This is the another fast Find() function.
//
// value if not 0 is set to the value found.
//
// Returns a struct QsDictionary for key or 0 if not found.
// This is used to concatenate a series of finds to get to
// a series of values or just the last leaf value in the series.
//
// For example:
//
//    you need the value at the key string "1:2:foo"
//    and you know there are values at "1:" and "1:2:",
//    so you can get the node (1) at "1:" and then from that
//    node get the node (2) at "2:" that is in node 1, and lastly
//    get the value at "1:2:foo" from node 2.  Like so:
//
//  struct QsDictionary *d = qsDictionaryFindDict(dict, "1:", 0);
//  d = qsDictionaryFindDict(d, "2:", 0);
//  void * value = qsDictionaryFind(d, "foo");
//
//  So in this process we concatenated the search like so: "1:2:foo" by
//  breaking it into 3 searches  "1:", "2:", and "foo" and the crazy thing
//  is, is that this is faster than 1 search method because we do not have
//  to concatenate to build the 1 string we are searching for.
//
extern
struct QsDictionary
*qsDictionaryFindDict(const struct QsDictionary *dict,
        const char *key, void **value);

extern
void qsDictionaryPrintDot(const struct QsDictionary *dict, FILE *file);


// Searches through the whole data structure.
//
// Do not screw with the key memory that is passed to callback().
//
// If callback returns non-zero the callback stops being called and the
// call to qsDictionaryForEach() returns.
//
// Searches the entire data structure starting at dict.  Calls callback
// with key set (if non-zero) and value.
//
// Returns the number of keys and callbacks called.
//
extern 
size_t
qsDictionaryForEach(const struct QsDictionary *dict,
        int (*callback) (const char *key, const void *value));
