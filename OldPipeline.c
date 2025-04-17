/*
* Pipeline.c
*
*/

#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>
#include <semaphore.h>
#include <fcntl.h>

#include "Pipeline.h"

// used to check whether a value to null, when it shouldn't be
#define check_null(val, msg, ret) ({                \
    if (val == NULL) {                              \
        if (errno != 0) perror(msg);                \
        else fprintf(stderr, "%s\n", msg);          \
        return ret;                                 \
    }                                               \
})                                                  

// used to exit from child processes
#define exit_child(msg) ({                          \
    process_print(child_print_sem, msg);            \
    sem_close(child_pause_sem);                     \
    sem_close(child_print_sem);                     \
    sem_unlink(PAUSE_SEM);                          \
    sem_unlink(PRINT_SEM);                          \
    exit(EXIT_FAILURE);                             \
})

// used to exit from the parent process in recurse_pipeline
#define exit_recurse(msg) ({                \
    if (errno != 0) perror(msg);            \
    else fprintf(stderr, "%s\n", msg);      \
    raise(FAIL_SIG);                        \
    return;                                 \
})

// used to exit from the main process before we start the recursion
#define exit_main(msg) ({                   \
    if (errno != 0) perror(msg);            \
    else fprintf(stderr, "%s\n", msg);      \
    goto end;                               \
})

typedef struct FDNode {
    struct FDNode* next;
    int p1_input;
    int p1_output;
    int p2_input;
    int p2_output;
} FDNode;

typedef struct FDList {
    FDNode* start_node;
    FDNode* end_node;
} FDList;

typedef struct FunctionNode {
    struct FunctionNode* next;
    Function f;
} FunctionNode;

struct pipeline {
    int length;
    FunctionNode* start_node;
    FunctionNode* end_node;
};

// SIGUSR1 and SIGUSR2 are used as a misc signals for developers
#define EXIT_SIG SIGUSR1 // finished recursion
#define FAIL_SIG SIGUSR2 // failed recursion

// these are used in the main process, across functions
sem_t* print_sem; // can we print in a process
sem_t* pause_sem; // whether we should pause execution

// used across processes
sem_t pnum_sem;  // used to count the child processes

#define PRINT_SEM "/print_sem" 
#define PAUSE_SEM "/pause_sem" 

static bool FDList_add(FDList* list, int p1_input, int p1_output, int p2_input, int p2_output) {
    if (list == NULL) return false;

    // create node
    FDNode* node = (FDNode*)malloc(sizeof(FDNode));
    if (node == NULL) return false;
    node->p1_input = p1_input;
    node->p1_output = p1_output;
    node->p2_input = p2_input;
    node->p2_output = p2_output;
    node->next = NULL;

    // append node
    if (list->start_node == NULL) {
        list->start_node = node;
        list->end_node = node;
    // something has gone wrong
    } else if (list->end_node == NULL) { 
        return false;
    } else {
        list->end_node->next = node;
        list->end_node = node;
    }

    return true;
}

static void FDList_free(FDList* list) {
    if (list == NULL) return;

    // frees elements in linked list
    FDNode* temp;
    while (list->start_node != NULL) {
        temp = list->start_node;
        list->start_node = list->start_node->next;
        close(temp->p1_input);
        close(temp->p1_output);
        close(temp->p2_input);
        close(temp->p2_output);
        free(temp);
    }

    free(list);
}

// used to print while recursion may be going on
static void process_print(sem_t* print_sem, const char* msg) {
    if (sem_wait(print_sem) == 0) {
        if (errno != 0) perror("");
        fprintf(stderr, "process: %d, erorr: %s\n", getpid(), msg);
        sem_post(print_sem);
    }
}

Pipeline* new_Pipeline() {
    Pipeline* p = (Pipeline*)malloc(sizeof(Pipeline));
    check_null(p, "pipeline not able to be instansiated", NULL);
    p->start_node = NULL;
    p->end_node = NULL;
    p->length = 0;
    return p;
}

bool Pipeline_add(Pipeline* p, Function f) {
    check_null(p, "pipeline null", false);
    check_null(f, "function null", false);

    // create node
    FunctionNode* node = (FunctionNode*)malloc(sizeof(FunctionNode));
    check_null(node, "pipeline node not able to be instansiated", false);
    node->f = f;
    node->next = NULL;

    // append node
    if (p->start_node == NULL) {
        p->start_node = node;
        p->end_node = node;
    // something has gone wrong
    } else if (p->end_node == NULL) { 
        fprintf(stderr, "end_node not set to null\n");
        return false;
    } else {
        p->end_node->next = node;
        p->end_node = node;
    }

    p->length++;

    return true;
}

// executed when child process exits
static void child_handler(int sig __attribute__((unused))) {

    sem_t* print_sem = sem_open(PRINT_SEM, 0);
    sem_t* pause_sem = sem_open(PAUSE_SEM, 0);

    // gets all hanging children
    while (1) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) {
            break;
        }
        // if process exited with exit() or _exit
        if (!WIFEXITED(status)) {
            process_print(print_sem, "child process exited abnormally");
            sem_post(pause_sem);
            raise(FAIL_SIG);
            return;
        }
        // if child process exited in failure
        if (WEXITSTATUS(status) != EXIT_SUCCESS) {
            process_print(print_sem, "child process exited in failure");
            sem_post(pause_sem);
            raise(FAIL_SIG);
            return;
        }
        sem_trywait(&pnum_sem);
    }
    
    // if last child terminated (or we fail to get a value), raise recurse exit
    int pnum_val;
    if (sem_getvalue(&pnum_sem, &pnum_val) < 0 || pnum_val <= 0) {
        sem_post(pause_sem); // if we fail to get a value, there may be more child processes, so we should stop more from spawning
        raise(EXIT_SIG);
        return;
    }
}

static void recurse_pipeline(FunctionNode* node, int input, FDList* fd_list) {
    if (node == NULL) return;

    // is execution paused?
    int pause_execution;
    if (sem_getvalue(pause_sem, &pause_execution) < 0 || pause_execution)
        exit_recurse("sem_getvalue pause_sem");

    int p1[2];
    int p2[2];

    if (pipe(p1) < 0) exit_recurse("pipe p1");
    if (pipe(p2) < 0) exit_recurse("pipe p2");
    if (!FDList_add(fd_list, p1[0], p1[1], p2[0], p2[1]))
        exit_recurse("FDList_add");

    // establishes signal handler which is called when SIGCHLD is recieved, and calls childhandler
    struct sigaction act;
    act.sa_flags = SA_SIGINFO;
    act.sa_handler = &child_handler;
    if (sigaction(SIGCHLD, &act, NULL) < 0)
        exit_recurse("sigaction");

    // forks process
    pid_t pid = fork();
    if (pid < 0) exit_recurse("fork");

    // if fork() == 0, then we are in the child process
    if (pid == 0) {

        // opens semaphores in child process
        sem_t* child_print_sem = sem_open(PRINT_SEM, 0);
        sem_t* child_pause_sem = sem_open(PAUSE_SEM, 0);

        // unused, since we take the last input as the input pipe
        if (close(p1[0]) < 0)
            exit_child("execution stopped");
        
        // copies input to p2[0]
        if (input != -1 && dup2(input, p2[0]) < 0)
            exit_child("failed to copy to input pipe");

        // copies p1[1] to p2[1]
        if (dup2(p1[1], p2[1]) < 0)
            exit_child("failed to copy to output pipe");

        // runs function stored in node
        node->f(p2[0], p2[1]); // may exit

        sem_close(child_pause_sem);
        sem_close(child_print_sem);
        exit(EXIT_SUCCESS);

    // fork() > 0, then we are in the parent process
    // since we don't wait on the child process to finish, this runs immediately
    } else recurse_pipeline(node->next, p1[0], fd_list);
}
   

void Pipeline_execute(Pipeline* p) {
    if (p == NULL) {
        fprintf(stderr, "pipeline null\n");
        return;
    }

    // declared here to avoid jump issues
    int signum = 0;
    sigset_t sig_set;
    int sig;
    FDList* fd_list = NULL;
    
    // insansiate semaphores
    print_sem = sem_open(PRINT_SEM, O_CREAT, 0644, 1);
    pause_sem = sem_open(PAUSE_SEM, O_CREAT, 0644, 0);
    if (sem_init(&pnum_sem, 0, p->length) < 0) {
        perror("sem_init pnum_sem");
        goto post_end; // since we can't close what we failed to open
    }

    // create exit signal, which the program will wait for
    if (sigemptyset(&sig_set) < 0) exit_main("sigemptyset");
    if (sigaddset(&sig_set, EXIT_SIG) < 0) exit_main("sigaddset exit_sig");
    if (sigaddset(&sig_set, FAIL_SIG) < 0) exit_main("sigaddset fail_sig");
    if (sigprocmask(SIG_BLOCK, &sig_set, NULL) < 0) exit_main("sigprocmask");

    // creates linked list to store file descriptors
    fd_list = (FDList*)malloc(sizeof(FDList));
    if (fd_list == NULL) exit_main("malloc fdlist");
    fd_list->start_node = NULL;
    fd_list->end_node = NULL;

    recurse_pipeline(p->start_node, -1, fd_list); // start pipeline execution

    // wait on all processes to finish, or for the exit signal
    signum = sigwait(&sig_set, &sig);
    if (signum == 0) {
        if (sig == FAIL_SIG) kill(1, SIGTERM); // kills all children
    } else {
        fprintf(stderr, "sigwait failed\n");
        kill(1, SIGTERM); // kills all children, just to make sure
    }
    end:
        sem_close(&pnum_sem);
    post_end:
        sem_close(pause_sem);
        sem_close(print_sem);
        sem_unlink(PRINT_SEM);
        sem_unlink(PAUSE_SEM);
        FDList_free(fd_list); // close all open pipes
}

void Pipeline_free(Pipeline* p) {
    if (p == NULL) {
        fprintf(stderr, "pipeline null\n");
        return;
    }

    // frees elements in linked list
    FunctionNode* temp;
    while (p->start_node != NULL) {
        temp = p->start_node;
        p->start_node = p->start_node->next;
        free(temp);
    }

    free(p);
}