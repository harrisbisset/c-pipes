/*
* Pipeline.c
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>
#include <semaphore.h>

#include "Pipeline.h"

int child_termination = 0; // used to check whether a child terminated
int function_success = 0; // used to check whether a function caused the child process to fail
sem_t pnum;

typedef struct FunctionNode {
    struct FunctionNode* next;
    Function f;
} FunctionNode;

struct pipeline {
    int length;
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

// used to create a catch function for a child
#define create_child_catch() ({                     \
    struct sigaction act;                           \
    act.sa_sigaction = child_handler;               \
    sigemptyset(&act.sa_mask);                      \
    act.sa_flags = SA_SIGINFO;                      \
    sigaction(SIGCHLD, &act, NULL);                 \
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

    this->length++;

    return true;
}

// executed 
static void child_handler(int sig, siginfo_t *info, void *ucontext) {
    sem_wait(&pnum);

    // if (sig != SIGCHLD) return;
    int status;
    int pid;
    pid = wait(&status);

    // if process exited with exit() or _exit
    if (!WIFEXITED(status)) {
        fprintf(stderr, "process %i: child process exited abnormally\n", pid);
        return;
    }
    // if child process exited in failure
    if (WEXITSTATUS(status) != EXIT_SUCCESS) {
        if (!function_success) fprintf(stderr, "function caused child process to fail\n"); // if function caused child process to fail
        fprintf(stderr, "process %i: child process exited in failure\n", pid);
        return;
    }

    // err(close(p1[1]), 0, "closing output fd");
    // if (input != -1) err(close(input), 0, "closing input fd");
        
    int pnum_val;
    sem_getvalue(&pnum, &pnum_val);
    printf("%i\n", pnum_val);
    if (pnum_val <= 0) {
        printf("sent: %i\n", getpid());
        raise(SIGUSR1);
        return;
    }
    create_child_catch();
}


static void recurse_pipeline(FunctionNode* node, int input) {
    if (node == NULL) return;

    pid_t pid;
    int p1[2];
    int p2[2];
    int function_success = 0; // used to check whether a function caused the child process to fail

    err(pipe(p1), 0, "creating pipeline");
    err(pipe(p2), 0, "creating pipeline");

    create_child_catch();

    pid = fork();
    err(pid, 0, "creating a fork");

    // if fork() == 0, then we are in the child process
    if (pid == 0) {
        err(p1[0], 1, "closing fd"); // unused, since we take the last input as the input pipe

        if (input != -1) err(dup2(input, p2[0]), 1, "failed to copy to input pipe");
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
        recurse_pipeline(node->next, p1[0]);
    }

    printf("recursed\n");

    // close file descriptors if any still open
    // if (input != -1) close(input);
    // close(p1[0]);
    // close(p1[1]);
    // close(p2[0]);
    // close(p2[1]);
}


void Pipeline_execute(Pipeline* this) {
    if (this == NULL) {
        fprintf(stderr, "pipeline null");
        return;
    }

    sigset_t sig_set;
    int sig;
    sigemptyset(&sig_set);
    sigaddset(&sig_set, SIGUSR1); // SIGUSR1 used as it's a misc signal for developers, in this case it means finished recursion
    sigprocmask(SIG_BLOCK, &sig_set, NULL);



    sem_init(&pnum, 1, this->length);
    recurse_pipeline(this->start_node, -1);
    printf("finished\n");


    // wait on all processes to finish
    printf("this pid: %i\n", getpid());
    int result = sigwait(&sig_set, &sig);
    if (result == 0) {
        err(sem_destroy(&pnum), false, "failed to destroy pnum semaphore"); 
        printf("got signal\n");
    } else {
    printf("dome\n");

    }
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