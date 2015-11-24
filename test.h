/*
 * Copyright (C) 2015 Arnaud Mouiche <arnaud.mouiche@invoxia.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

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
