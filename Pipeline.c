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

#define err(val, child, msg) ({                     \
    if (val < 0) {                                  \
        perror(msg);                                \
        if (child) exit(EXIT_FAILURE);              \
        else return;                                \
    }                                               \
})                                                  \

Pipeline* new_Pipeline() {
    Pipeline* p = (Pipeline*)malloc(sizeof(Pipeline));
    p->start_node = NULL;
    p->end_node = NULL;
    return p;
}

bool Pipeline_add(Pipeline* this, Function f) {
    if (this == NULL || f == NULL) return false;

    // create node
    FunctionNode* node = (FunctionNode*)malloc(sizeof(FunctionNode));
    if (node == NULL) return false; // memory not allocated
    node->f = f;
    node->next = NULL;

    // append node
    if (this->start_node == NULL) {
        this->start_node = node;
        this->end_node = node;
    } else if (this->end_node == NULL) {
        fprintf(stderr, "end_node not set to null, when it's expected to be\n");
        return false; // something's gone wrong
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
    if (this == NULL) return;
    recurse_pipeline(this->start_node, STDIN_FILENO);
}


void Pipeline_free(Pipeline* this) {
    if (this == NULL) return;

    // frees elements in linked list
    FunctionNode* temp;
    while (this->start_node != NULL) {
        temp = this->start_node;
        this->start_node = this->start_node->next;
        free(temp);
    }

    free(this);
}