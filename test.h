

#ifndef __test_h__
#define __test_h__

#include "alsa.h"

extern struct ev_loop *loop; /* this is the event loop */
extern unsigned cumul_seq_errors;  /* total number of sequence errors detected */

struct test;

struct test_ops {
    int (*start)(struct test *t);
    int (*close)(struct test *t);
};

/*
 * base class for all tests
 */
struct test {
    const char *name;
    char device[64];
    struct alsa_config config;

    const struct test_ops *ops;
};


#endif //__test_h__
