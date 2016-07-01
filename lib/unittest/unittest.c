// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2013, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/*
 * Functions for unit tests.  See lib/unittest/include/unittest.h for usage.
 */
#include <assert.h>
#include <compiler.h>
#include <debug.h>
#include <err.h>
#include <platform.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unittest.h>

/**
 * \brief Default function to dump unit test results
 *
 * \param[in] line is the buffer to dump
 * \param[in] len is the length of the buffer to dump
 * \param[in] arg can be any kind of arguments needed to dump the values
 */
static void default_printf (const char *line, int len, void *arg)
{
    printf ("%s", line);
}

// Default output function is the printf
static test_output_func out_func = default_printf;
// Buffer the argument to be sent to the output function
static void *out_func_arg = NULL;

/**
 * \brief Function called to dump results
 *
 * This function will call the out_func callback
 */
void unittest_printf (const char *format, ...)
{
    static char print_buffer[PRINT_BUFFER_SIZE];

    va_list argp;
    va_start (argp, format);

    if (out_func != NULL) {
        // Format the string
        vsnprintf(print_buffer, PRINT_BUFFER_SIZE, format, argp);
        out_func (print_buffer, PRINT_BUFFER_SIZE, out_func_arg);
    }

    va_end (argp);
}

bool expect_bytes_eq(const uint8_t *expected, const uint8_t *actual, size_t len,
                     const char *msg)
{
    if (memcmp(expected, actual, len)) {
        printf("%s. expected\n", msg);
        hexdump8(expected, len);
        printf("actual\n");
        hexdump8(actual, len);
        return false;
    }
    return true;
}

void unittest_set_output_function (test_output_func fun, void *arg)
{
    out_func = fun;
    out_func_arg = arg;
}

#if defined(WITH_LIB_CONSOLE)
#include <lib/console.h>

// External references to the testcase registration tables.
extern unittest_testcase_registration_t __start_unittest_testcases[] __WEAK;
extern unittest_testcase_registration_t __stop_unittest_testcases[] __WEAK;

static void usage(const char* progname) {
    printf("Usage:\n"
           "%s <case>\n"
           "  where case is a specific testcase name, or...\n"
           "  all : run all tests\n"
           "  ?   : list tests\n",
           progname);

}

static void list_cases(void) {
    char   fmt_string[32];
    size_t count = 0;
    size_t max_namelen = 0;

    const unittest_testcase_registration_t* testcase;
    for (testcase  = __start_unittest_testcases;
         testcase != __stop_unittest_testcases;
         ++testcase) {

        if (testcase->name) {
            size_t namelen = strlen(testcase->name);
            if (max_namelen < namelen)
                max_namelen = namelen;
            count++;
        }
    }

    printf("There %s %zu test case%s available...\n",
            count == 1 ? "is" : "are",
            count,
            count == 1 ? "" : "s");
    snprintf(fmt_string, sizeof(fmt_string), "  %%-%zus : %%s\n", max_namelen);

    for (testcase  = __start_unittest_testcases;
         testcase != __stop_unittest_testcases;
         ++testcase) {

        if (testcase->name)
            printf(fmt_string, testcase->name,
                   testcase->desc ? testcase->desc : "<no description>");
    }

}


bool run_unittest(const unittest_testcase_registration_t* testcase) {
    char   fmt_string[32];
    size_t max_namelen = 0;
    size_t passed = 0;

    DEBUG_ASSERT(testcase);
    DEBUG_ASSERT(testcase->name);
    DEBUG_ASSERT(!!testcase->tests == !!testcase->test_cnt);

    for (size_t i = 0; i < testcase->test_cnt; ++i) {
        const unittest_registration_t* test = &testcase->tests[i];
        if (test->name) {
            size_t namelen = strlen(test->name);
            if (max_namelen < namelen)
                max_namelen = namelen;
        }
    }
    snprintf(fmt_string, sizeof(fmt_string), "  %%-%zus : ", max_namelen);

    printf("%s : Running %zu test%s...\n",
           testcase->name,
           testcase->test_cnt,
           testcase->test_cnt == 1 ? "" : "s");

    void* context = NULL;
    status_t init_res = testcase->init ? testcase->init(&context) : NO_ERROR;
    if (init_res != NO_ERROR) {
        printf("%s : FAILED to initialize testcase! (status %d)", testcase->name, init_res);
        return false;
    }

    lk_bigtime_t testcase_start = current_time_hires();

    for (size_t i = 0; i < testcase->test_cnt; ++i) {
        const unittest_registration_t* test = &testcase->tests[i];

        printf(fmt_string, test->name ? test->name : "");

        lk_bigtime_t test_start = current_time_hires();
        bool good = test->fn ? test->fn(context) : false;
        lk_bigtime_t test_runtime = current_time_hires() - test_start;

        if (good) {
            passed++;
        } else {
            printf(fmt_string, test->name ? test->name : "");
        }

        printf("%s (%lld.%03lld mSec)\n",
               good ? "PASSED" : "FAILED",
               test_runtime / 1000,
               test_runtime % 1000);

    }

    lk_bigtime_t testcase_runtime = current_time_hires() - testcase_start;

    printf("%s : %sll tests passed (%zu/%zu) in %lld.%03lld mSec\n",
           testcase->name,
           passed != testcase->test_cnt ? "Not a" : "A",
           passed, testcase->test_cnt,
           testcase_runtime / 1000,
           testcase_runtime % 1000);

    return passed == testcase->test_cnt;
}

static int run_unittests(int argc, const cmd_args* argv)
{
    if (argc != 2) {
        usage(argv[0].str);
        return 0;
    }

    const char* casename = argv[1].str;

    if (!strcmp(casename, "?")) {
        list_cases();
        return 0;
    }

    bool run_all = !strcmp(casename, "all");
    const unittest_testcase_registration_t* testcase;
    size_t chosen = 0;
    size_t passed = 0;

    for (testcase  = __start_unittest_testcases;
         testcase != __stop_unittest_testcases;
         ++testcase) {

        if (testcase->name) {
            if (run_all || !strcmp(casename, testcase->name)) {
                chosen++;
                passed += run_unittest(testcase) ? 1 : 0;
                printf("\n");

                if (!run_all)
                    break;
            }
        }
    }

    if (!run_all && !chosen) {
        printf("Test case \"%s\" not found!\n", casename);
        list_cases();
    } else {
        printf("Passed %zu/%zu test case%s\n", passed, chosen, chosen == 1 ? "" : "s");
    }

    return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("ut", "Run unittests", run_unittests)
STATIC_COMMAND_END(unittests);

#endif
