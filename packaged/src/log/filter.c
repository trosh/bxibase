/* -*- coding: utf-8 -*-
 ###############################################################################
 # Author: Pierre Vigneras <pierre.vigneras@bull.net>
 # Created on: May 24, 2013
 # Contributors:
 ###############################################################################
 # Copyright (C) 2012  Bull S. A. S.  -  All rights reserved
 # Bull, Rue Jean Jaures, B.P.68, 78340, Les Clayes-sous-Bois
 # This is not Free or Open Source software.
 # Please contact Bull S. A. S. for details about its license.
 ###############################################################################
 */

#include <string.h>
#include <search.h>

#include "bxi/base/err.h"
#include "bxi/base/mem.h"
#include "bxi/base/str.h"
#include "bxi/base/time.h"
#include "bxi/base/zmq.h"

#include "bxi/base/log.h"
#include "bxi/base/log/level.h"
#include "bxi/base/log/filter.h"

//*********************************************************************************
//********************************** Defines **************************************
//*********************************************************************************

//*********************************************************************************
//********************************** Types ****************************************
//*********************************************************************************

//*********************************************************************************
//********************************** Static Functions  ****************************
//*********************************************************************************
//static int _filter_compar(const void * filter1, const void* filter2);
//static void _merge_filter_visitor(const void *nodep, const VISIT which, const int depth);

//*********************************************************************************
//********************************** Global Variables  ****************************
//*********************************************************************************

const bxilog_filter_s BXILOG_FILTER_ALL_ALL_S = {.prefix = "", .level = BXILOG_ALL};
const bxilog_filter_s BXILOG_FILTER_ALL_OUTPUT_S = {.prefix = "", .level = BXILOG_OUTPUT};

const bxilog_filter_s * BXILOG_FILTER_ALL_LOWEST = &BXILOG_FILTER_ALL_ALL_S;
const bxilog_filter_s * BXILOG_FILTER_ALL_OUTPUT = &BXILOG_FILTER_ALL_OUTPUT_S;

const bxilog_filters_s BXILOG_FILTERS_ALL_OFF_S = {.allocated=false,
                                                   .nb=0,
                                                   .allocated_slots=0,
                                                   .list = {NULL},
                                                  };
const bxilog_filters_s BXILOG_FILTERS_ALL_OUTPUT_S = {.allocated=false,
                                                      .nb=1,
                                                      .allocated_slots=1,
                                                      .list = {(bxilog_filter_p)
                                                               &BXILOG_FILTER_ALL_OUTPUT_S},
                                                     };
const bxilog_filters_s BXILOG_FILTERS_ALL_ALL_S = { .allocated=false,
                                                    .nb = 1,
                                                    .allocated_slots = 1,
                                                    .list = {(bxilog_filter_p)
                                                             &BXILOG_FILTER_ALL_ALL_S},
                                                  };

bxilog_filters_p BXILOG_FILTERS_ALL_OFF = (bxilog_filters_p) &BXILOG_FILTERS_ALL_OFF_S;
bxilog_filters_p BXILOG_FILTERS_ALL_OUTPUT = (bxilog_filters_p) &BXILOG_FILTERS_ALL_OUTPUT_S;
bxilog_filters_p BXILOG_FILTERS_ALL_ALL = (bxilog_filters_p) &BXILOG_FILTERS_ALL_ALL_S;

//*********************************************************************************
//********************************** Implementation    ****************************
//*********************************************************************************


bxilog_filters_p  bxilog_filters_new() {
    size_t slots = 2;
    bxilog_filters_p filters = bximem_calloc(sizeof(*filters) +\
                                             slots*sizeof(filters->list[0]));
    filters->allocated = true;
    filters->nb = 0;
    filters->allocated_slots = slots;

    return filters;
}

void bxilog_filters_free(bxilog_filters_p filters) {
    if (NULL == filters) return;
    if (!filters->allocated) return;

    for (size_t i = 0; i < filters->nb; i++) {
        BXIFREE(filters->list[i]->prefix);
        BXIFREE(filters->list[i]);
    }
    BXIFREE(filters);
}

void bxilog_filters_destroy(bxilog_filters_p * filters_p) {
    bxiassert(NULL != filters_p);

    bxilog_filters_free(*filters_p);
    *filters_p = NULL;
}

void bxilog_filters_add(bxilog_filters_p * filters_p,
                        const char * prefix, bxilog_level_e level) {

    bxiassert(NULL != filters_p);
    bxiassert(NULL != *filters_p);
    bxiassert(NULL != prefix);

    bxilog_filter_p filter = bximem_calloc(sizeof(*filter));
    filter->prefix = strdup(prefix);
    filter->level = level;

    bxilog_filters_p filters = *filters_p;
    filters->list[filters->nb] = filter;
    filters->nb++;

    if (filters->nb >= filters->allocated_slots) {
        size_t old = filters->allocated_slots;
        size_t new = old * 2;
        filters = bximem_realloc(filters,
                                 sizeof(*filters) + old*sizeof(filters->list[0]),
                                 sizeof(*filters) + new*sizeof(filters->list[0]));
        filters->allocated_slots = new;
        *filters_p = filters;
    }
}

bxilog_filters_p  bxilog_filters_dup(bxilog_filters_p filters) {
    bxiassert(NULL != filters);
    bxilog_filters_p result = bxilog_filters_new();
    result->allocated = true;
    for (size_t i = 0; i < filters->nb; i++) {
        bxilog_filter_p filter = filters->list[i];
        bxilog_filters_add(&result, (char*) filter->prefix, filter->level);
    }

    return result;
}

bxierr_p bxilog_filters_parse(char * str, bxilog_filters_p * result) {
    bxiassert(NULL != str);
    bxiassert(NULL != result);

    str = strdup(str); // Required because str might not be allocated on the heap
    bxierr_p err = BXIERR_OK, err2 = BXIERR_OK;

    *result = NULL;
    char *saveptr1 = NULL;
    char *str1 = str;

    bxilog_filters_p filters = bxilog_filters_new();

    for (size_t j = 0; ; j++, str1 = NULL) {
        char * token = strtok_r(str1, ",", &saveptr1);
        if (NULL == token) break;
        char * sep = strchr(token, ':');
        if (sep == NULL) {
            err2 = bxierr_gen("Expected ':' in log level configuration: %s", token);
            BXIERR_CHAIN(err, err2);
            goto QUIT;
        }

        *sep = '\0';
        char * prefix = token;
        char * level_str = sep + 1;
        bxilog_level_e level;
        unsigned long tmp;
        errno = 0;

        char * endptr;
        tmp = strtoul(level_str, &endptr, 10);
        if (0 != errno) {
            err2 = bxierr_errno("Error while parsing number: '%s'",
                                level_str);
            BXIERR_CHAIN(err, err2);
            goto QUIT;
        }
        if (endptr == level_str) {
            err2 = bxilog_level_from_str(level_str, &level);
            BXIERR_CHAIN(err, err2);
            if (bxierr_isko(err)) {
                goto QUIT;
            }
        } else if (*endptr != '\0') {
            err = bxierr_errno("Error while parsing number: '%s' doesn't contain only "
                               "a number or a level", level_str);
            BXIERR_CHAIN(err, err2);
            goto QUIT;
        } else {
            if (tmp > BXILOG_LOWEST) {
                err2 = bxierr_new(BXILOG_BADLVL_ERR,
                                  (void*) tmp, NULL, NULL, NULL,
                                  "Bad log level: %lu, using %d instead",
                                  tmp, BXILOG_LOWEST);
                BXIERR_CHAIN(err, err2);
                tmp = BXILOG_LOWEST;
            }
            level = (bxilog_level_e) tmp;
        }
        bxilog_filters_add(&filters, prefix, level);
    }

    *result = filters;

QUIT:
    BXIFREE(str);
    if (bxierr_isko(err)) bxilog_filters_destroy(&filters);
    return err;
}

void bxilog_filter_add_merge_to(bxilog_filter_p filter1, bxilog_filter_p filter2,
                                bxilog_filters_p * result) {

    bxiassert(NULL != filter1);
    bxiassert(NULL != filter2);

    const size_t f1_len = strlen(filter1->prefix);
    const size_t f2_len = strlen(filter2->prefix);

    bxilog_filter_p shortest, longest;
    size_t shortest_len;

    // It is preferable to keep the order so we use '<=' instead of '<'
    if (f1_len <= f2_len) {
        shortest = filter1;
        longest = filter2;
        shortest_len = f1_len;
    } else {
        shortest = filter2;
        longest = filter1;
        shortest_len = f2_len;
    }

    const int cmp = strncmp(shortest->prefix, longest->prefix, shortest_len);

    if (cmp == 0) {
        bxilog_level_e highest_level = (shortest->level > longest->level) ?
                                                       shortest->level : longest->level;
        if (f1_len == f2_len) {
            // Equals prefix: merge result is the one with the highest level

            bxilog_filters_add(result, shortest->prefix, highest_level);
            return;
        }
        // Shortest is a prefix
        bxilog_filters_add(result, shortest->prefix, highest_level);
        bxilog_filters_add(result, longest->prefix, highest_level);
        return;
    }

    // No prefix found, add both of them with their own level
     bxilog_filters_add(result, shortest->prefix, shortest->level);
     bxilog_filters_add(result, longest->prefix, longest->level);
     return;
}


bxilog_filters_p bxilog_filters_merge(bxilog_filters_p * filters_array, size_t n) {
    bxiassert(NULL != filters_array || 0 == n);

    bxilog_filters_p result = bxilog_filters_new();

    for (size_t i = 0; i < n; i++) {
        bxilog_filters_p filters = filters_array[i];
        for (size_t k = 0; k < filters->nb; k++) {
            bxilog_filter_p filter = filters->list[k];

            // Then compare with other filters
            for (size_t j = i + 1; j < n; j++) {
                bxilog_filters_p other_filters = filters_array[j];
                for (size_t l = 0; l < other_filters->nb; l++) {
                    bxilog_filter_p other_filter = other_filters->list[l];

                    bxilog_filter_add_merge_to(filter, other_filter, &result);
               }
            }
        }
    }

    return result;
}

//bxilog_filters_p bxilog_filters_merge(bxilog_filters_p * filters_array, size_t n) {
//    bxiassert(NULL != filters_array || 0 == n);
//
//    // We first copy all filters into a new array. Two reasons:
//    // 1. some filters might have been statically allocated and are therefore not
//    //    modifiable
//    // 2. we will destroy a binary tree at the end, and therefore, all filters inserted
//    //    into it.
//    // Therefore, it is better to use our own copy
//    bxilog_filters_p * copied = bximem_calloc(n * sizeof(*copied));
//    for (size_t i = 0; i < n; i++) {
//        bxilog_filters_p filters = filters_array[i];
//        copied[i] = bxilog_filters_dup(filters);
//    }
//
//    bxilog_filters_p result = bxilog_filters_new();
//    // tsearch root node
//    void * root = NULL;
//
//    for (size_t i = 0; i < n; i++) {
//        bxilog_filters_p filters = copied[i];
//
//        for (size_t k = 0; k < filters->nb; k++) {
//            bxilog_filter_p filter = filters->list[k];
//            void * val = tsearch(filter, &root, _filter_compar);
//            bxiassert(NULL != val);
//            bxilog_filter_p found = *(bxilog_filter_p *) val;
//            found->level = found->level > filter->level ? found->level : filter->level;
//            found->reserved = &result;
//        }
//    }
//
//    // Sort all filters and populate the result
//    twalk(root, _merge_filter_visitor);
//
//#ifdef _GNU_SOURCE
//    tdestroy(root, NULL);
//#else
//    while (NULL != root) {
//        bxilog_filter_p filter = *(bxilog_filter_p *) root;
//        tdelete(filter, &root, _filter_compar);
//    }
//#endif
//
//    for (size_t i = 0; i < n; i++) {
//        bxilog_filters_destroy(&copied[i]);
//    }
//    BXIFREE(copied);
//
//    return result;
//}

//*********************************************************************************
//********************************** Static Helpers Implementation ****************
//*********************************************************************************
//int _filter_compar(const void * filter1, const void * filter2) {
//    bxiassert(NULL != filter1);
//    bxiassert(NULL != filter2);
//
//    bxilog_filter_p f1 = (bxilog_filter_p) filter1;
//    bxilog_filter_p f2 = (bxilog_filter_p) filter2;
//    size_t f1_len = strlen(f1->prefix);
//    size_t f2_len = strlen(f2->prefix);
//
//    size_t shortest = (f1_len < f2_len) ? f1_len : f2_len;
//    int cmp = strncmp(f1->prefix, f2->prefix, shortest);
//    int result;
//    if (cmp == 0) {
//        result = (int) (f1_len - f2_len);
//    } else {
//        result = cmp;
//    }
//
//    printf("pref(%s:%zu, %s:%zu)=%d %zu\n", f1->prefix, f1_len, f2->prefix, f2_len, result, shortest);
//    return result;
//}

//void _merge_filter_visitor(const void *nodep, const VISIT which, const int depth) {
//    UNUSED(depth);
//    bxilog_filter_p filter = *(bxilog_filter_p *) nodep;
//    bxilog_filters_p * result = (bxilog_filters_p *) filter->reserved;
//
//    switch (which) {
//        case preorder:
//            break;
//        case postorder:
//            printf("%s\n", filter->prefix);
//            bxilog_filters_add(result, (char*) filter->prefix, filter->level);
//            break;
//        case endorder:
//            break;
//        case leaf:
//            printf("%s\n", filter->prefix);
//            bxilog_filters_add(result, (char*) filter->prefix, filter->level);
//            break;
//    }
//}

