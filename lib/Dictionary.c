#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "Dictionary.h"
#include "../lib/debug.h"

//  126  ~      is the last nice looking ASCII char
//   32  SPACE  is the first nice looking ASCII char

// We return error on insert if any input character keys are outside this
// range.  We only store the "nice" visible ASCII characters.  This is
// hard enough without thinking about UTF-8.


#define START          (32) // start at SPACE
#define END            (126) // ~

//#define ALPHABET_SIZE  (END + 1 - START) // 95 characters.


// A Dictionary via a Trie
//
// Faster than a hash table, and just a little more memory usage.
// This code ROCKS!
//
// References:
// We use something with less memory than in this:
// https://www.geeksforgeeks.org/trie-insert-and-search/
//
// https://en.wikipedia.org/wiki/Trie
//
// We optimise far more than these references, but they are a nice place
// to start to try to understand this code.
//
//
struct QsDictionary {

    // A Dictionary node.

    // For our use case:
    //   - We do not need fast insert.
    //   - We do not need remove at all.
    //   - Just need fast lookup.
    //   - This will hold about 1000 elements in extreme cases.

    // If there are children than it's an array of length 4.
    //
    // There is no benefit to using a constant variable or CPP MACRO that
    // is 4 because we need to do bit diddling too, that is 3, 2, and 1
    // are constants that are needed too.  The 4 is like a natural
    // constant like Pi in our use case.  Hiding the 4 would just obscure
    // the code.
    //
    // We use a reduced alphabet of size 4 to index this array, otherwise
    // it's 0.  We add a little complexity by having Null children to
    // start with.  We reduce the alphabet to needing 2 bits (2^2=4) as we
    // traverse through the branch points in the tree.
    //
    // Traversing the tree graph example:
    //
    // Trie containing keys "hello" and "heat"
    //
    //     ROOT
    //       |
    //       he____
    //        |    |
    //        llo  at
    //
    //
    // "e" is a branch point that is common to the two keys
    // In ASCII
    //   'e' = oct 0145 = hex 0x65 = dec 101 = binary 01100101 = 01 10 01 01
    //   'l' = oct 0154 = hex 0x6C = dec 108 = binary 01101100 = 01 10 11 00
    //   'a' = oct 0141 = hex 0x61 = dec  97 = binary 01100001 = 01 10 00 01
    //
    // "hello" deviates from "heat" at the 'l' and the 'a':
    // and so the deviation is at the first and second bits being
    //  00 ('l') and 01 ('a') when we fix the 2 bit left to right ordering,
    // so the hello traversal reads like 'h' 'e' 00 11 10 01 'l' 'o'
    // or                                'h' 'e' 0  3  2  1  'l' 'o'
    // adding 1 to the 2 bit values =>   'h' 'e' 1  4  3  2  'l' 'o'
    // and since ASCII decimal 1 2 3 and 4 are invalid characters in our
    // use we can use them is encode the missing 'l' as the 8 bit byte
    // sequence 1 4 3 2.  We can't use 0 because that would terminate our
    // string, so we are using 'l' = (c0-1) << 0 + (c1-1) << 2 + (c2-1) << 4
    // + (c3-1) << 6 where c0 = ASCII byte 1, c1 = ASCII byte 4,
    // c2 = ASCII byte 3, and c3 = ASCII byte 2.
    //
    // Doing the same analysis for "heat" gives
    //
    // "heat" traversal is   'h' 'e' 1 0 2 1 't'
    // adding the 1       => 'h' 'e' 2 1 3 2 't'
    //
    // At the cost of converting characters after branch points we can use
    // a lot less memory.  Each branch point can only have a 1 2 3 or 4
    // as a starting character.
    //
    // The example tree looks like;
    //
    //     ROOT
    //       |
    //       he_______________
    //        |               |
    //       \1\4\3\2lo      \2\1\3\2t
    //
    //
    // where we define \1 as the bit sequence 01 that we convert to 00,
    // and so on.
    //
    //
    // Adding key = "heep" gives ==> he\2\2\3\2p
    //
    //
    //     ROOT
    //       |
    //       he_________________
    //        |                 |
    //        |                 |
    //       \1\4\3\2lo         \2___________________
    //                           |                    |
    //                           |                    |
    //                          \1\3\2t              \2\3\2p
    //
    //
    // where \1 \2 \3 \4 are just one byte characters that are 1 2 3 4.
    // We will store them as bytes even though they only contain 2 bits
    // of information, saving computation time at the cost of using a
    // little more memory at branch points.
    //
    // But wait there's more.  The top branch point is from the "he" where
    // e is shared below.  But we can't use 'e' are a branch point, this
    // 'e' must be made into a 2 bit group like so:  'e' = \2\2\3\2  so:
    //
    //
    //
    //     ROOT
    //       |
    //       h\2\2\3\2__________________
    //               |                  |
    //               |                  |
    //              \1\4\3\2lo         \2____________________
    //                                  |                    |
    //                                  |                    |
    //                                 \1\3\2t              \2\3\2p
    //
    //
    // But wait there's even move.  The 'h' can't be a branch point from
    // root so  'h' = ASCII 150 = binary 01101000 = 01 10 10 00
    // = \2 \3 \3 \1 = \1\3\3\2 in left to right order so:
    //
    //     ROOT
    //       |
    //      \1\3\3\2\2\2\3\2__________________
    //                     |                  |
    //                     |                  |
    //                    \1\4\3\2lo         \2____________________
    //                                        |                    |
    //                                        |                    |
    //                                       \1\3\2t              \2\3\2p
    //
    //
    //
    // Clearly with such short keys there is more stuff like \1\2\3\4 than
    // there would be had they been longer keys.
    //
    //
    //
    struct QsDictionary *children; // array of 4 when needed and allocated

    // suffix is a string added to the current path traversal
    // generated string.  It can be 0 if no chars are needed.
    //
    // If an insert is done making the suffix become incorrect the node
    // must be broken, this node will loose as much of the extra
    // characters as it needs to and add children as it needs to, to make
    // all the traversals correct.  This helps us use much less memory,
    // by having more information in a node.  This little addition to the
    // "standard" Trie data structure adds a little complexity to
    // Insert(), but not much to Find().  We don't bother making "" empty
    // strings; we just make them 0 pointers.
    //
    const char *suffix;

    // value stored if there is one.  The key for this value is the string
    // generated by traversing the graph stringing together the characters
    // and optional suffix (if present) at each node in the traversal.
    // Sometimes this node is just a branch point with no value.
    const void *value;
};


struct QsDictionary *qsDictionaryCreate(void) {

    struct QsDictionary *d = calloc(1, sizeof(*d));
    ASSERT(d, "calloc(1,%zu) failed", sizeof(*d));

    struct QsDictionary *children = calloc(4, sizeof(*children));
    ASSERT(children, "calloc(%d,%zu) failed", 4, sizeof(*children));
    d->children = children;

    return d;
}


static
void FreeChildren(struct QsDictionary *children) {

    for(struct QsDictionary *i = children + 4 - 1;
            i >= children; --i)
        if(i->children)
            // Recurse
            FreeChildren(i->children);
#if DEBUG
    memset(children, 0, 4*sizeof(*children));
#endif
    free(children);
}


void qsDictionaryDestroy(struct QsDictionary *dict) {

    DASSERT(dict);

    if(dict->children)
        FreeChildren(dict->children);

#if DEBUG
    memset(dict, 0, sizeof(*dict));
#endif
    free(dict);
}


// Returns true is we are at a regular character as we have defined them
// or false if there are those funny 2 bit characters in the queue.
static inline
bool CheckChars(char **bits[], const char **c) {

    // We cannot call this function if there are no more characters left
    // in the string.
    DASSERT(**c);

    return (**bits)?false:true;
}


// Returns the next character
// This returns the next character after we run out of the 2 bit encoded
// characters.
static inline
char GetChar(char **bits[], const char **c) {

    // We cannot call this function if there are no more characters left
    // in the string.
    DASSERT(**c);

    if(**bits) {
        // For this use case we encode the 0 as 1 and the 1 as 2
        // and the 2 as 3 and the 3 as 4; because we need 0 to terminate
        // the string.
        DASSERT((***bits) < 4);
        char ret = (***bits) + 1;
        *bits += 1;
        if(**bits == 0)
            // Advance to the next character for next time.
            *c += 1;
        // Return a 2 bits of a byte.
        return ret;
    }

    // *bits is at the null terminator.

    *c += 1;
    
    return (**c);
}


// I wish C had pass by reference.  This is only used to index into a
// child node.
static inline
char GetBits(char **bits[], const char **c) {

    // We cannot call this function if there are no more characters left
    // in the string.
    DASSERT(**c);

    char ret;

    if(**bits) {
        ret = ***bits;
        *bits += 1;
        if(**bits == 0)
            // Advance to the next character for next time.
            *c += 1;
        // Return a 2 bits of a byte.
        return ret;
    }

    // *bits is at the null terminator.

    // The current character we analyze is:
    ret = **c;

    // Bit diddle with next character, i.e. decompose it into 4 2 bit
    // pieces in bits[].  We store 2 bits in each char in bits[4].  The
    // value of any one of bits[] is 0, 1, 2, or 3.  We encode 1 8 bit
    // char as 4 8 byte chars with values only 0, 1, 2, or 3.

    // Go to the second array element in this null terminate array of 4 +
    // 1 null = *bits[5].
    //
    *bits -= 3;

    // We can use a pointer to a 32 bit = 4 byte x 8 bit/byte thing to set
    // all the *bits[] array at one shot.
    uint32_t *val32 = (uint32_t *) (*(*bits - 1));

    // We must keep this order when decoding in the Find() function.

    *val32 =   ((0x00000003) & ret) // mask out just the first 2 bits
            | (((0x0000000C) & ret) << 6)// mask 2 bits and shift
            | (((0x00000030) & ret) << 12) // mask 2 bits and shift
            | (((0x000000C0) & ret) << 18); // mask 2 bits and shift
    //
    // The next bits[] to be returned after this call will be **bits =
    // *bits[0], not *bits[-1].
    //
    return **(*bits -1); // Return the first set of 2 bits.
}


// Returns 0 on success or 1 if already present and -1 if it is not added
// and have an invalid character.
int qsDictionaryInsert(struct QsDictionary *node,
        const char *key, const void *value) {

    DASSERT(node);
    DASSERT(key);
    DASSERT(*key);
    DASSERT(value);

    // Since we have no need for speed in Insert and the code is simpler
    // this way, we check all the characters in the key at the start of
    // this function.  It would have be faster (but more complex) to do
    // this as we traverse the characters in the key in the next for loop.
    //
    for(const char *c = key; *c; ++c)
        if(*c < START || *c > END) {
            ERROR("Invalid character in key: \"%s\"",
                    key);
            return -1;
        }

    // Setup pointers to an array of chars that null terminates.
    char *b[5];
    char **bits = b + 4;
    char bit[4];
    memset(bits, 0, 4);
    b[0] = bit;
    b[1] = bit + 1;
    b[2] = bit + 2;
    b[3] = bit + 3;
    b[4] = 0; // null terminate.
    // We'll use bits to swim through this "bit" character array.

#if 0
    for(const char *str = key; *str;)
        fprintf(stderr, "\\%d", GetBits(&bits, &str));
    fprintf(stderr, "\n");

    return 0;
#endif


    for(const char *c = key; *c;) {

        // We iterate through the "key" either 2 bits at a time or one
        // char at a time, depending on what the current apparent
        // traversal char value is which we use GetBits() and GetChar() to
        // iterate c for us.
        //
        // We cannot call GetBits() or GetChar() without knowing that
        // *c != 0.

        if(node->children) {
            // We will go to next child in the traversal.
            //
            // Go to the next child in the traversal.
            //
            node = node->children + GetBits(&bits, &c);

            if(node->suffix) {
                // We have a suffix in this node.
                //
                // Set up two sets of character iterators: c and e.
                //
                const char *e = node->suffix; // one for current suffix

                // Find the point where key and suffix do not match.
                for(;*e && *c;) {
                    // Consider iterating over 2 bit chars
                    if(*e < START && (*e) - 1 != GetBits(&bits, &c))
                        break;
                    // Else iterate over regular characters.
                    if(*e != GetChar(&bits, &c))
                        break;
                    ++e;
                }

///// HERE LANCE

                // CASES:
                //
                //  The only way to understand this is to draw pictures
                //  like you are a 6 year old kid.  Or maybe a 3 year old
                //  for all you prodigies.
                //
                // 1. (*e == '\0')
                //    suffix ran out
                //    => MATCH or next node
                //
                // 2. (*c == '\0')
                //    key ran out
                //    => INSERT split the node adding one children set
                //
                // 3. (*e && e != node->suffix)
                //    neither ran out and they do not match
                //    => BIFURCATE making two child nodes
                //

                if(*e == '\0') {
                    // 1. MATCH  so far.
                    continue;
                }

                if(*c == '\0') { // There are unmatched chars in suffix
                    //
                    // 2. INSERT a node
                    //
                    // We matched part way through the suffix.
                    //
                    // So: split the node in two and the first one has the
                    // new inserted value and the second has the old value
                    // and the old children.
                    //
                    // New node children:
                    struct QsDictionary *children =
                            calloc(4, sizeof(*children));
                    ASSERT(children, "calloc(4,%zu) failed",
                            sizeof(*children));

                    // parent -> node1 (firstChar + start of suffix) ->
                    //   node2 (ee* + rest of suffix) -> ...

                    // dummy pointer node will be acting as node1.

                    struct QsDictionary *node2 = children + (*ee) - 1;
                    // node2 has all the children of node and the old
                    // value and the remaining suffix that was in node.
                    node2->children = node->children;
                    node2->value = node->value;
                    // node2 just has part of the old suffix.
                    if(*(ee+1)) {
                        node2->suffix = strdup(ee+1);
                        ASSERT(node2->suffix, "strdup(%p) failed", ee+1);
                    }

                    const char *oldSuffix = node->suffix;
                    // Now remake node
                    node->value = value; // it has the new value.
                    if(ee == node->suffix) {
                        // There where no matching chars in suffix
                        // and the char pointers never advanced.
                        node->suffix = 0;
                    } else {
                        // At least one char matched in suffix.  Null
                        // terminate the suffix after the last matching
                        // char and then dup it, and free the old remains
                        // of suffix.
                        * (char *) ee = '\0';
                        node->suffix = strdup(oldSuffix);
                        ASSERT(node->suffix, "strdup(%p) failed",
                                oldSuffix);
                    }
                    free((char *)oldSuffix);
                    // This node now gets the new children and has one
                    // child that is node2 from above.
                    node->children = children;
                    return 0; // success
                }

                // BIFURCATE
                //
                // We match up to "ee" but there is more to go and we
                // don't match after that.  Result is we need to add 2
                // nodes.

                // New node children:
                //
                // 2 of these children will be used in this bifurcation.
                struct QsDictionary *children =
                        calloc(4, sizeof(*children));
                ASSERT(children, "calloc(%d,%zu) failed",
                        4, sizeof(*children));

                struct QsDictionary *n1 = children + (*ee) - START;
                struct QsDictionary *n2 = children + (*cc) - START;
                const char *oldSuffix = node->suffix;
                n1->value = node->value;
                n1->children = node->children;
                n2->value = value;

                node->children = children;
                node->value = 0;

                if(*(ee+1)) {
                    n1->suffix = strdup(ee+1);
                    ASSERT(n1->suffix, "strdup(%p) failed", ee+1);
                }

                ++cc;
                if(*cc) {
                    n2->suffix = strdup(cc);
                    ASSERT(n2->suffix, "strdup(%p) failed", cc);
                }

                if(ee == node->suffix)
                    node->suffix = 0;
                else {
                    *((char *)ee) = '\0';
                    node->suffix = strdup(oldSuffix);
                    ASSERT(node->suffix, "strdup(%p) failed", oldSuffix);
                }

                // We have copied all that we needed from this old suffix.
                free((char *) oldSuffix);

                return 0; // success
            }

            ++c;

            continue; // See if there are more children.
        }

        // We have no more children
        //
        // Make children, insert and return.
        //
        DASSERT(node->children == 0);

        if(node->value == 0) {
            DASSERT(node->suffix == 0);
            if(*c) {
                node->suffix = strdup(c);
                ASSERT(node->suffix, "strdup(%p) failed", c);
            }
            node->value = value;
            return 0; // success
        }

        struct QsDictionary *children =
                calloc(4, sizeof(*children));
        ASSERT(children, "calloc(%d,%zu) failed",
                4, sizeof(*children));
        node->children = children;

        // go to this character (*c) node.
        node = children + (*c) - START;
        node->value = value;
        // add any suffix characters if needed.
        ++c;
        if(*c) {
            // TODO: check c characters are valid.
            node->suffix = strdup(c);
            ASSERT(node->suffix, "strdup(%p) failed", c);
        }
        return 0; // success

    } // for() on characters *c


    // We have no more characters and the last matching node is node.

    if(node->value) {
        ERROR("We have an entry with key=\"%s\"", key);
        return 1;
    }

    // node->value == 0
    node->value = value;
    return 0; // success done
}


// Returns element ptr.
void *qsDictionaryFind(const struct QsDictionary *node, const char *key) {

    for(const char *c = key; *c;) {
        if(node->children) {

            DASSERT(*c >= START);
            DASSERT(*c <= END);
            if(*c < START || *c > END) {
                ERROR("Invalid character in key=\"%s\"", key);
                return 0;
            }
            node = node->children + (*c) - START;
            ++c;

            if(node->suffix) {

                const char *ee = node->suffix;

                for(; *ee && *c && *c == *ee; ++c, ++ee)
                    if(*c < START || *c > END) {
                        ERROR("Invalid character in key=\"%s\"", key);
                        return 0;
                    }

                if(*ee == 0)
                    // Matched so far
                    continue;
                else {
                    // The suffix has more characters that we did not
                    // match.
                    ERROR("No key=\"%s\" found ee=\"%s\"  c=\"%s\"",
                            key, ee, c);
                    return 0;
                }
            }
            continue;
        }

        // No more children, but we have unmatch key characters.
        ERROR("No key=\"%s\" found", key);
        return 0;
    }

    // Hooray!  We got it.
    return (void *) node->value;
}


static void
PrintChildren(const struct QsDictionary *node, char *lastString, FILE *f) {

    if(node->children) {
        struct QsDictionary *end = node->children + 4;
        char c = START;
        for(struct QsDictionary *child = node->children;
                child < end; ++child, ++c) {
            const char *suffix = child->suffix;
            suffix = (suffix?suffix:"");

            if(child->children || child->value) {

                const char *suffix = ((child->suffix)?(child->suffix):"");
                const char *value =
                    ((child->value)?((const char *) child->value):"");

                size_t len = strlen(lastString) + 1 + strlen(suffix) + 1;
                char *str = malloc(len);
                ASSERT(str, "malloc(%zu) failed", len);

                if(child->value)
                    // Create child node id
                    fprintf(f, "  \"%s%c%s\" [label=\"%c%s\\n|%s|\"];\n",
                            lastString, c, suffix, c, suffix, value);
                else
                    // Create child node id
                    fprintf(f, "  \"%s%c%s\" [label=\"%c%s\"];\n",
                            lastString, c, suffix, c, suffix);


                snprintf(str, len, "%s%c%s", lastString, c, suffix);

                PrintChildren(child, str, f);

                free(str);

                fprintf(f, "  \"%s\" -> \"%s%c%s\";\n",
                        lastString, lastString, c, suffix);
            }
        }
    }
}


void qsDictionaryPrintDot(const struct QsDictionary *node, FILE *f) {

    fprintf(f, "digraph {\n  label=\"Trie Dictionary\";\n\n");

    fprintf(f, "  \"\" [label=\"ROOT\"];\n");


    PrintChildren(node, "", f);

    fprintf(f, "}\n");
}