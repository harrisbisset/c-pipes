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
#include <fcntl.h>

#include "Pipeline.h"

static void err(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

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

static void close_fd(int fd) {
    do {
        if (close(fd) < 0) {
            perror("closing fd");
            fprintf(stderr, "%s:%d: closing file descriptor: %d\n", __FILE__, __LINE__, fd);
        }
    } while (0);
}

static void dup_fd2(int oldfd, int newfd) {
    if (oldfd == newfd) return;
    printf("hello1\n\n");

    dup2(oldfd, newfd);
    printf("hello2\n\n");

    close_fd(oldfd);
    printf("hello3\n\n");
}

static void recurse_pipeline(FunctionNode* node, int input) {
    if (node == NULL) return;

    pid_t parent;
    int p1[2];
    int p2[2];

    if (pipe(p1) < 0) err("creating pipeline");
    if (pipe(p2) < 0) err("creating pipeline");

    parent = fork();
    if (parent < 0) err("creating a fork");

    // if fork() == 0, then we are in the child process
    if (parent == 0) {
        close_fd(p1[0]); //unused

        if (dup2(input, p2[0]) == -1) /*read from in_fd */
            perror("Failed to redirect stdin");
        if (dup2(p1[1], p2[1]) == -1)   /*write to fd[1]*/
            perror("Failed to redirect stdout");

        // dup_fd2(input, STDIN_FILENO);
        // dup_fd2(p[1], STDOUT_FILENO);

        node->f(p2[0], p2[1]);

        exit(EXIT_SUCCESS);

    // fork() > 0, then we are in the parent process
    } else {
        int status;
        if (wait(&status) < 0) err("waiting for the child process to finish");

        // if (WIFEXITED(status) != 0) err("exited abnormally");
        // if (WEXITSTATUS(status) != EXIT_SUCCESS) err("exited in failure");

        close(p1[1]);
        close(input);

        recurse_pipeline(node->next, p1[0]);
        // exit(EXIT_SUCCESS);
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