/*
* Pipeline.c
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include "Pipeline.h"

#define err(val, what) ({ \
    if ((val) == -1) { \
        perror((what)); \
        exit(EXIT_FAILURE); \
    } \
    return; \
})

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
    } else if (this->end_node == NULL) return false; // something's gone wrong
    else {
        this->end_node->next = node;
        this->end_node = node;
    }

    return true;
}

static void recurse_pipeline(FunctionNode* node, int input) {
    if (node == NULL) return;

    pid_t parent;
    int p[2];

    err(pipe(p), "creating pipeline");

    parent = fork();
    err(parent, "creating a fork");

    // if fork() == 0, then we are in the child process
    if (!parent) {
        
        if (input != -1) err(dup2(input, p[0]), "duplicating an fd");

        // close since we're not using them here
        close(p[1]);
        close(input);

        node->f(p[0], p[1]);

        exit(EXIT_FAILURE);

    } else {
        int status;
        waitpid(-1, &status, 0);
        err(dup2(p[1], input),  "duplicating an fd"); // p[1] == execve() stdout

        close(p[0]);

        recurse_pipeline(node->next, input);
        exit(EXIT_FAILURE);
    }
}

void Pipeline_execute(Pipeline* this) {
    if (this == NULL) return;
    recurse_pipeline(this->start_node, -1);
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