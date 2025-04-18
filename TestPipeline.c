#include "Pipeline.h"
#include "PipelineHelper.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/wait.h>
#include <pthread.h>

#define DEBUG 0

#define create_pipeline()               \
    printf("testing: %s\n", __func__);  \
    Pipeline *p = new_Pipeline();       \
    if (p == NULL) cleanupExit(p);      \
    Pipeline_setDebug(p, DEBUG);

#define add_pipeline(f) \
    if (!Pipeline_add(p, f)) cleanupExit(p);

#define execute_return(expected) ({                     \
    Pipeline_execute(p);                                \
    int result = Pipeline_exitCode(p);                  \
    Pipeline_free(p);                                   \
    return result == expected;                          \
})

#define add_pipeline_multi(max, f) ({                   \
    for (int i = 0; i < max; i++) {                     \
        add_pipeline(f);                                \
    }                                                   \
})

// used to help debug
#define print_process() \
    if (DEBUG) printf("%s: process %i, parent: %i\n", __func__, getpid(), getppid());

static void cleanupExit(Pipeline *p) {
    Pipeline_free(p);
}

static void success(int input, int output) {
    close(input);
    close(output);
    return;
}

static void exit_fail(int input, int output) {
    close(input);
    close(output);
    exit(EXIT_FAILURE);
}

static void exit_success(int input, int output) {
    close(input);
    close(output);
    exit(EXIT_SUCCESS);
}

// generate 1 to 20
static void generate(int input, int output) {
    close(input);
    int i = 0;
    for (i = 1; i < 21; i++) {
        write(output, &i, sizeof(int));
    }
    i = -1;
    write(output, &i, sizeof(int));
}

// multiplies each number it gets by incrmenting values, starting from 2
static void multiply(int input, int output) {
    int number = 0, i = 1;
    while (1) {
        i++;
        read(input, &number, sizeof(int));
        if (number == -1) break;
        number = number * i;
        write(output, &number, sizeof(int));
    }
    i = -1;
    write(output, &i, sizeof(int));
}

static int function_success() {
    create_pipeline();
    add_pipeline(success);
    execute_return(0);
}

static int function_exit_success() {
    create_pipeline();
    add_pipeline(exit_success);
    execute_return(-1);
}

static int function_exit_failure() {
    create_pipeline();
    add_pipeline(exit_fail);
    execute_return(-1);
}

static int function_success_multi() {
    create_pipeline();
    add_pipeline_multi(100, success);
    execute_return(0);
}

static int function_exit_success_multi() {
    create_pipeline();
    add_pipeline_multi(10, exit_success);
    execute_return(-1);
}

static int function_exit_failure_multi() {
    create_pipeline();
    add_pipeline_multi(10, exit_fail);
    execute_return(-1);
}

static int function_multiply() {
    create_pipeline();
    add_pipeline(generate);
    add_pipeline_multi(40, multiply);
    execute_return(0);
}

static void generateInts(int input, int output) {
    close(input); // not used in first pipeline stage
    if (DEBUG) printf("generateInts: process %i, parent %i\n", getpid(), getppid());
    int i = 0;
    for (i = 2; i <= 10000; i++) {
        write(output, &i, sizeof(int));
    }
    i = -1;
    write(output, &i, sizeof(int)); // write -1 to indicate end
}

static void sieveInts(int input, int output) {
    int prime, number = 0;
    read(input, &prime, sizeof(int));
    if (DEBUG) printf("sieveInts: process %i, parent %i: prime = %i\n", getpid(), getppid(), prime);
    if (prime == -1) { // this means we have already got to the end
        write(output, &prime, sizeof(int)); // pass on -1 terminator to child sieve stage
    } else {
        while (number != -1) {
            read(input, &number, sizeof(int));
            if (number % prime != 0) {
                write(output, &number, sizeof(int));
            }
        }
    }
}

static int sieve_ints() {
    create_pipeline();
    add_pipeline(generateInts);
    add_pipeline_multi(1000, sieveInts);
    execute_return(0);
}

static void generateIntsSum(int input, int output) {
    close(input); // not used in first pipeline stage
    if (DEBUG) printf("generateInts: process %i, parent %i\n", getpid(), getppid());
    for (int i = 1; i <= 1000; i++) {
        write(output, &i, sizeof(int));
    }
}
 
static void squareInts(int input, int output) {
    if (DEBUG) printf("squareInts: process %i, parent %i\n", getpid(), getppid());
    for (int i = 1; i <= 1000; i++) {
        int number;
        read(input, &number, sizeof(int));
        int result = number * number;
        write(output, &result, sizeof(int));
    }
}
 
 
static void sumIntsAndPrint(int input, int output) {
    close(output); // not used in last pipeline stage
    if (DEBUG) printf("sumIntsAndPrint: process %i, parent %i\n", getpid(), getppid());
    int number = 0;
    int result = 0;
    for (int i = 1; i <= 100; i++) {
        read(input, &number, sizeof(int));
        result += number;
    }
    if (DEBUG) printf("sumIntsAndPrint: result = %i\n", result);
}

static int sum_squares() {
    create_pipeline();
    add_pipeline(generateIntsSum);
    add_pipeline(squareInts);
    add_pipeline(sumIntsAndPrint);
    execute_return(0);
}

static void pipeline_multiply(int input, int output) {
    create_pipeline();
    add_pipeline(generate);
    add_pipeline_multi(5, multiply);
    Pipeline_execute(p);
    int result = Pipeline_exitCode(p);
    Pipeline_free(p);
    assert(result == 0);
    return;
}

static int function_pipeline_pipeline() {
    create_pipeline();
    add_pipeline_multi(10, pipeline_multiply);
    execute_return(0);
}

static void pipeline_early_exit(int input, int output) {
    close(input);
    close(output);
    pthread_exit(0);
    printf("shouldn't be seen\n"); // never printed
    return;
}


static int function_thread_fail() {
    create_pipeline();
    add_pipeline(pipeline_early_exit); // thread terminates early, but doesn't hang process
    add_pipeline(generate);
    add_pipeline(multiply);
    // add_pipeline(pipeline_early_exit); !!!! if the function were placed here, the program would hang
    add_pipeline(multiply);
    add_pipeline(multiply);
    execute_return(0);
}

int main() {

    assert(function_success());
    assert(function_exit_success());
    assert(function_exit_failure());

    assert(function_success_multi());
    assert(function_exit_success_multi());
    assert(function_exit_failure_multi());

    assert(function_multiply());

    assert(sieve_ints());
    assert(sum_squares());

    assert(function_thread_fail()); // example case edge case

    assert(function_pipeline_pipeline()); // pipeline the pipelines

    return 0;
}