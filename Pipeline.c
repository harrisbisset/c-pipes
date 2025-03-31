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

#define err(what, val) \
    (val == -1) ? \
        printf("something went wrong when %s: %s\n", what, strerror(errno)) \
    : 0 \

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


void Pipeline_execute(Pipeline* this) {
    if (this == NULL) return;
    
    pid_t pid;
    int p[2];
    int first = 1;

    err("creating a pipe", pipe(p));

    while (this->start_node != NULL) {
        pid = fork();
        err("creating a fork", pid);

        if (first) first = 0;
        else {
            err("switching file descriptors", dup2(0, p[1]));
            err("creating a new file descriptors", dup(p[0]));
        }


        this->start_node->f(p[0], p[1]);

        while (wait(NULL) >= 0);
        this->start_node = this->start_node->next;
    }

    close(p[0]);
    close(p[1]);
}


void Pipeline_free(Pipeline* this) {
    if (this == NULL) return;
    free(this);
}