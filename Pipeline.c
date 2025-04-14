/*
* Pipeline.c
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include "Pipeline.h"

typedef struct FunctionNode {
    struct FunctionNode* next;
    Function f;
} FunctionNode;

struct pipeline {    
    FunctionNode* start_node;
    FunctionNode* end_node;
};

// used to check whether an error value is returned from a function inside pipeline execution
#define err(val, child, msg) ({                     \
    if (val < 0) {                                  \
        if (errno != 0) perror(msg);                \
        if (child) exit(EXIT_FAILURE);              \
        else return;                                \
    }                                               \
})                                                  \

// used to check whether a value to null, when it shouldn't be
#define check_null(val, msg, ret) ({                \
    if (val == NULL) {                              \
        if (errno != 0) perror(msg);                \
        fprintf(stderr, "%s\n", msg);               \
        return ret;                                 \
    }                                               \
})                                                  \

Pipeline* new_Pipeline() {
    Pipeline* p = (Pipeline*)malloc(sizeof(Pipeline));
    check_null(p, "pipeline not able to be instansiated", NULL);
    p->start_node = NULL;
    p->end_node = NULL;
    return p;
}

bool Pipeline_add(Pipeline* this, Function f) {
    check_null(this, "pipeline null", false);
    check_null(f, "function null", false);

    // create node
    FunctionNode* node = (FunctionNode*)malloc(sizeof(FunctionNode));
    check_null(node, "pipeline node not able to be instansiated", false);
    node->f = f;
    node->next = NULL;

    // append node
    if (this->start_node == NULL) {
        this->start_node = node;
        this->end_node = node;
    // something has gone wrong, likely user has modified end_node
    } else if (this->end_node == NULL) { 
        fprintf(stderr, "end_node not set to null\n");
        return false;
    } else {
        this->end_node->next = node;
        this->end_node = node;
    }

    return true;
}

static void recurse_pipeline(FunctionNode* node, int input) {
    if (node == NULL) return;

    pid_t pid;
    int p1[2];
    int p2[2];
    int function_success = 0; // used to check whether a function caused the child process to fail

    err(pipe(p1), 0, "creating pipeline");
    err(pipe(p2), 0, "creating pipeline");

    pid = fork();
    err(pid, 0, "creating a fork");

    // if fork() == 0, then we are in the child process
    if (pid == 0) {
        err(p1[0], 1, "closing fd"); // unused

        err(dup2(input, p2[0]), 1, "failed to copy to input pipe");
        err(dup2(p1[1], p2[1]), 1, "failed to copy to output pipe");

        function_success = 0;
        node->f(p2[0], p2[1]);
        function_success = 1;

        // if the function set an error, but didn't exit
        if (errno != 0) {
            perror("function failed");
            exit(EXIT_FAILURE);
        };

        exit(EXIT_SUCCESS);

    // fork() > 0, then we are in the parent process
    } else {

        // waits for the child process (0) to finish
        int status;
        err(waitpid(pid, &status, 0), 0, "waitpid failed");
        
        // if process exited with exit() or _exit
        if (!WIFEXITED(status)) {
            fprintf(stderr, "child process exited abnormally\n");
            return;
        }
        // if child process exited in failure
        if (WEXITSTATUS(status) != EXIT_SUCCESS) {
            if (!function_success) fprintf(stderr, "function caused child process to fail\n"); // if function caused child process to fail
            fprintf(stderr, "process %i: child process exited in failure\n", getpid());
            return;
        }

        err(close(p1[1]), 0, "closing output fd");
        err(close(input), 0, "closing input fd");

        recurse_pipeline(node->next, p1[0]);
    }
}

void Pipeline_execute(Pipeline* this) {
    if (this == NULL) {
        fprintf(stderr, "pipeline null");
        return;
    }
    recurse_pipeline(this->start_node, STDIN_FILENO);
}

void Pipeline_free(Pipeline* this) {
    if (this == NULL) {
        fprintf(stderr, "pipeline null");
        return;
    }

    // frees elements in linked list
    FunctionNode* temp;
    while (this->start_node != NULL) {
        temp = this->start_node;
        this->start_node = this->start_node->next;
        free(temp);
    }

    free(this);
}