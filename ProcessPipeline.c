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
    process_print(msg);                             \
    return NULL;                                    \
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

#define p_execution() ({                    \
    pthread_mutex_lock(&pause_lock);        \
    pause_exe = true;                       \
    pthread_mutex_unlock(&pause_lock);      \
})

typedef struct thread_struct {
    Function f;
    int input;
    int p1[2];
    int p2[2];
} thread_struct;

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
#define FAIL_SIG SIGUSR1 // failed recursion
#define EXIT_SIG SIGUSR2 // exited recursion

#define max_threads 6

pthread_t tid [max_threads];
pthread_mutex_t print_lock;
pthread_mutex_t count_lock;
pthread_mutex_t pause_lock;
pthread_mutex_t fd_list_lock;
pthread_mutex_t available_lock;
pthread_mutex_t threads_lock;
bool pause_exe;
int pnum;
FDList* fd_list;
int available_threads;

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
static void process_print(const char* msg) {
    pthread_mutex_lock(&print_lock);
    if (errno != 0) perror("");
    fprintf(stderr, "process: %d, erorr: %s\n", getpid(), msg);
    pthread_mutex_unlock(&print_lock);
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
    // gets all hanging children
    while (1) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) {
            break;
        }
        // if process exited with exit() or _exit
        if (!WIFEXITED(status)) {
            process_print("child process exited abnormally");
            p_executionI();
            raise(FAIL_SIG);
            return;
        }
        // if child process exited in failure
        if (WEXITSTATUS(status) != EXIT_SUCCESS) {
            process_print("child process exited in failure");
            p_execution();
            raise(FAIL_SIG);
            return;
        }
        pthread_mutex_lock(&pnum);
        pnum--;
        pthread_mutex_unlock(&pnum);
    }
    
    // if last child terminated (or we fail to get a value), raise recurse exit
    pthread_mutex_lock(&pnum);
    if (pnum <= 0) {
        p_execution(); // if we fail to get a value, there may be more child processes, so we should stop more from spawning
        pthread_mutex_unlock(&pnum);
    }
    pthread_mutex_unlock(&pnum);
}

static void* child(void* arg) {
    thread_struct* args = (thread_struct*) arg;
    if (args == NULL) exit_child("args null");

    pthread_mutex_lock(&available_lock);
    available_threads++;
    pthread_mutex_unlock(&available_lock);

    // unused, since we take the last input as the input pipe
    if (close(args->p1[0]) < 0) exit_child("execution stopped");
    
    // copies input to p2[0]
    if (args->input != -1 && dup2(args->input, args->p2[0]) < 0)
        exit_child("failed to copy to input pipe");

    // copies p1[1] to p2[1]
    if (dup2(args->p1[1], args->p2[1]) < 0)
        exit_child("failed to copy to output pipe");

    // runs function stored in node
    args->f(args->p2[0], args->p2[1]); // may exit

    pthread_mutex_lock(&fd_list_lock);
    // err(close(p1[1]), 0, "closing output fd");
    // if (input != -1) err(close(input), 0, "closing input fd");
    pthread_mutex_unlock(&fd_list_lock);

    // if we have available threads, let more spawn
    pthread_mutex_lock(&available_lock);
    available_threads++;
    if (available_threads > 0) pthread_mutex_unlock(&threads_lock);
    pthread_mutex_unlock(&available_lock);

    return NULL;
}

static void recurse_pipeline(int count, FunctionNode* node, int input) {
    if (node == NULL) return;

    // is execution paused?
    pthread_mutex_lock(&pause_lock);
    if (pause_exe) {
        fprintf(stderr, "execution paused\n");
        pthread_mutex_unlock(&pause_lock);
        raise(FAIL_SIG);
    }
    pthread_mutex_unlock(&pause_lock);

    // if we don't have spare thread, wait for some to become available
    pthread_mutex_lock(&available_lock);
    if (available_threads == 0) pthread_mutex_lock(&threads_lock);
    pthread_mutex_unlock(&available_lock);

    int p1[2];
    int p2[2];

    if (pipe(p1) < 0) exit_recurse("pipe p1");
    if (pipe(p2) < 0) exit_recurse("pipe p2");

    // adds file descriptors to mutex
    pthread_mutex_lock(&fd_list_lock);
    if (!FDList_add(fd_list, p1[0], p1[1], p2[0], p2[1])) {
        pthread_mutex_unlock(&fd_list_lock);
        exit_recurse("FDList_add");
    }
    pthread_mutex_unlock(&fd_list_lock);

    pthread_create(&(tid[count]), NULL, &child, NULL);

    recurse_pipeline(count++, node->next, p1[0]);
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
    pid_t pid;
    
    if (pthread_mutex_init(&pause_lock, NULL) < 0) exit_main("pause_lock");
    if (pthread_mutex_init(&print_lock, NULL) < 0) exit_main("print_lock");
    if (pthread_mutex_init(&count_lock, NULL) < 0) exit_main("count_lock");
    if (pthread_mutex_init(&fd_list_lock, NULL) < 0) exit_main("fd_list_lock");
    if (pthread_mutex_init(&threads_lock, NULL) < 0) exit_main("threads_lock");
    if (pthread_mutex_init(&available_lock, NULL) < 0) exit_main("available_lock");

    // create failure signal, which the program will wait for
    if (sigemptyset(&sig_set) < 0) exit_main("sigemptyset");
    if (sigaddset(&sig_set, FAIL_SIG) < 0) exit_main("sigaddset fail_sig");
    if (sigaddset(&sig_set, EXIT_SIG) < 0) exit_main("sigaddset sigkill");
    if (sigprocmask(SIG_BLOCK, &sig_set, NULL) < 0) exit_main("sigprocmask");

    // creates linked list to store file descriptors
    fd_list = (FDList*)malloc(sizeof(FDList));
    if (fd_list == NULL) exit_main("malloc fdlist");
    fd_list->start_node = NULL;
    fd_list->end_node = NULL;

    pid = fork();
    if (pid < 0) exit_main("fork");

    // in child process
    if (pid == 0) {
        recurse_pipeline(0, p->start_node, -1); // start pipeline execution

        // wait for running threads to terminate
        for (int i = 0; i < max_threads; i++) {
            if (pthread_join(tid[i], NULL) != 0) {
                raise(FAIL_SIG);
                return;
            }
        }

        // exited successfully
        raise(EXIT_SIG);
        return;

    // in parent process
    } else {

        // wait on all processes to finish, or for the exit signal
        signum = sigwait(&sig_set, &sig);
        if (signum == 0) {
            if (sig == FAIL_SIG) fprintf(stderr, "process failed");
        } else {
            fprintf(stderr, "sigwait failed\n");
            kill(1, SIGTERM); // kills all children, just to make sure
        }

        end:
            pthread_mutex_destroy(&pause_lock);
            pthread_mutex_destroy(&print_lock);
            pthread_mutex_destroy(&count_lock);
            pthread_mutex_destroy(&fd_list_lock);
            pthread_mutex_destroy(&available_lock);
            pthread_mutex_destroy(&threads_lock);
            FDList_free(fd_list); // close all open pipes
    }
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