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
#include <pthread.h> 
#include <fcntl.h>
#include <string.h>

#include "Pipeline.h"

#define process_print(msg) ({                                                               \
    if (debug) {                                                                            \
        if (errno != 0)                                                                     \
            fprintf(stderr, "process: %i, %s: %s\n", getpid(), msg, strerror(errno));       \
        else fprintf(stderr, "process: %i, %s\n", getpid(), msg);                           \
    }                                                                                       \
})

// used to check whether a value to null, when it shouldn't be
#define check_null(val, msg, ret) ({                \
    if (val == NULL) {                              \
        if (errno != 0) perror(msg);                \
        else fprintf(stderr, "%s\n", msg);          \
        return ret;                                 \
    }                                               \
})                                                  

// used to exit from threads
#define exit_thread(msg) ({                         \
    process_print(msg);                             \
    free(arg);                                      \
    _exit(EXIT_FAILURE);                             \
    return NULL;                                    \
})

// used to exit from the parent process in recurse_pipeline
#define exit_recurse(msg) ({                \
    process_print(msg);                     \
    _exit(EXIT_FAILURE);                     \
    return;                                 \
})

// used to exit from the main process before we start the recursion
#define exit_main(msg) ({                   \
    p->exit_code = -1;                      \
    process_print(msg);                     \
    goto end;                               \
})

typedef struct thread_struct {
    Function f;
    int debug;
    int input;
    int p1[2];
    int p2[2];
} thread_struct;

typedef struct FunctionNode {
    struct FunctionNode* next;
    Function f;
} FunctionNode;

struct pipeline {
    int length;
    int debug;
    int exit_code;
    FunctionNode* start_node;
    FunctionNode* end_node;
};

#define max_threads 6

Pipeline* new_Pipeline() {
    Pipeline* p = (Pipeline*)malloc(sizeof(Pipeline));
    check_null(p, "pipeline not able to be instansiated", NULL);
    p->start_node = NULL;
    p->end_node = NULL;
    p->length = 0;
    p->debug = 0;
    p->exit_code = -3; // if the exit_code were to be checked before running, it would be a user error, but it's set to -3 nevertheless
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
        return false;
    } else {
        p->end_node->next = node;
        p->end_node = node;
    }

    p->length++;

    return true;
}

static void* child(void* arg) {
    struct thread_struct* args = (thread_struct*) arg;
    if (args == NULL) {
        _exit(EXIT_FAILURE);
        return NULL;
    }
    int debug = args->debug; // for consistency accross macros

    if (debug) fprintf(stderr, "starting thread: %lu\n", pthread_self());

    // these should always be valid file descriptors
    // not super necessary, but it's nice to handle edge cases
    if (fcntl(args->p1[1], F_GETFD) < 0) exit_thread("p1[1] not a valid fd");
    if (fcntl(args->p2[0], F_GETFD) < 0) exit_thread("p2[0] not a valid fd");
    if (fcntl(args->p2[1], F_GETFD) < 0) exit_thread("p1[1] not a valid fd");
    if (args->input != -1 && fcntl(args->input, F_GETFD) < 0) exit_thread("input not a valid fd");

    // copies input to p2[0]
    if (args->input != -1 && dup2(args->input, args->p2[0]) < 0)
        exit_thread("failed to copy to input pipe");

    // copies p1[1] to p2[1]
    if (dup2(args->p1[1], args->p2[1]) < 0)
        exit_thread("failed to copy to output pipe");

    // runs function stored in node
    args->f(args->p2[0], args->p2[1]); // may exit

    if (close(args->p1[1]) < 0) exit_thread("closing output fd");
    if (args->input != -1 && close(args->input) < 0) exit_thread("closing input fd");

    if (debug) fprintf(stderr, "exiting thread: %lu\n", pthread_self());
    
    free(args);
    return NULL;
}

static void recurse_pipeline(pthread_t tid[], int count, int debug, FunctionNode* node, int input) {
    if (node == NULL) return;

    int p1[2];
    int p2[2];
    if (pipe(p1) < 0) exit_recurse("pipe p1");
    if (pipe(p2) < 0) exit_recurse("pipe p2");

    // list of args to pass into thread
    thread_struct* arg = (thread_struct*)malloc(sizeof(thread_struct));
    arg->f = node->f;
    arg->input = input;
    arg->debug = debug;
    arg->p1[0] = p1[0];
    arg->p1[1] = p1[1];
    arg->p2[0] = p2[0];
    arg->p2[1] = p2[1];

    // creates new thread
    if (pthread_create(&tid[count], NULL, &child, arg) != 0) exit_recurse("pthread_create");

    recurse_pipeline(tid, count+1, debug, node->next, p1[0]);
}

void Pipeline_execute(Pipeline* p) {
    if (p == NULL) return;
    p->exit_code = -1; // sudden exit means failure now

    int debug = p->debug; // for consistency accross macros

    // vars declared here to avoid jump issues
    pthread_t tid[p->length];
    int completion[2];

    // process pids
    pid_t container_pid;

    // private channel avoids shared names and state between executions.
    if (pipe(completion) < 0) {
        process_print("completion pipe");
        p->exit_code = -1;
        return;
    }
    for (int i = 0; i < 2; i++) {
        if (fcntl(completion[i], F_SETFD, FD_CLOEXEC) < 0 || fcntl(completion[i], F_SETFL, O_NONBLOCK) < 0) {
            close(completion[0]);
            close(completion[1]);
            process_print("completion pipe flags");
            p->exit_code = -1;
            return;
        }
    }

    if (debug) fprintf(stderr, "parent: %d, main child: %d\n", getppid(), getpid());

    container_pid = fork();
    if (container_pid < 0) exit_main("container fork");
    else if (container_pid == 0) { // container child process
        close(completion[0]); // only need the read end in the parent, not child

        if (debug) fprintf(stderr, "parent: %d, container child: %d\n", getppid(), getpid());
        recurse_pipeline(tid, 0, debug, p->start_node, -1); // start pipeline execution

        // wait for running threads to terminate
        for (int i = 0; i < p->length; i++) {
            if (debug) fprintf(stderr, "parent %i: waiting on %dth thread\n", getppid(), i);
            int error = pthread_join(tid[i], NULL);
            if (error != 0) {
                errno = error;
                exit_recurse("pthread_join");
            }
        }

        // report completion to the parent process
        const unsigned char success = 1;
        ssize_t written = write(completion[1], &success, sizeof(success));
        if (written != sizeof(success)) exit_recurse("write completion");  // expecting 1 char

        close(completion[1]);
        
        _exit(EXIT_SUCCESS);
        return;
        
    } else {
        close(completion[1]); // parent doesn't write, so we close
        completion[1] = -1; // marks as closed

        // wait for child to finish
        int status;
        pid_t np;
        do {
            np = waitpid(container_pid, &status, 0);
        } while (np < 0 && errno == EINTR); // blocking, so if process interrupted

        if (np < 0) exit_main("waitpid failed");

        if (debug) fprintf(stderr, "parent: %d, container parent: %d\n", getppid(), getpid());

        // if process didn't exit with exit() or _exit
        if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS)
            exit_main("container child exited abnormally");
        
        // early from a callback is not successful completion.
        unsigned char success = 0;
        ssize_t received = read(completion[0], &success, sizeof(success));
        if (received != sizeof(success) || success != 1) exit_main("missing completion");

        p->exit_code = 0; // success

        end:
            close(completion[0]);
            if (completion[1] != -1) close(completion[1]); // if not already closed

        return;
    }
}

void Pipeline_free(Pipeline* p) {
    if (p == NULL) return;

    // frees elements in linked list
    FunctionNode* temp;
    while (p->start_node != NULL) {
        temp = p->start_node;
        p->start_node = p->start_node->next;
        free(temp);
        temp = NULL;
    }

    free(p);
    return;
}

void Pipeline_setDebug(Pipeline* p, int debug) {
    if (p == NULL) return;
    p->debug = debug;
    return;
}

int Pipeline_exitCode(Pipeline* p) {
    if (p == NULL) return -2;
    return p->exit_code;
}
