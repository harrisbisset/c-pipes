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
#include <semaphore.h>

#include "Pipeline.h"

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
    sem_close(success_sem);                         \
    raise(SIGKILL);                                 \
    return NULL;                                    \
})

// used to exit from the parent process in recurse_pipeline
#define exit_recurse(msg) ({                \
    if (errno != 0) perror(msg);            \
    else fprintf(stderr, "%s\n", msg);      \
    sem_close(success_sem);                 \
    raise(SIGKILL);                         \
    return;                                 \
})

// used to exit from the main process before we start the recursion
#define exit_main(msg) ({                   \
    if (errno != 0) perror(msg);            \
    else fprintf(stderr, "%s\n", msg);      \
    goto end;                               \
})

// used to exit from the container parent process if the child fails
#define exit_container_parent(msg) ({       \
    fprintf(stderr, "%s\n", msg);           \
    sem_close(success_sem);                 \
    kill(getppid(), FAIL_SIG);              \
    return;                                 \
})

typedef struct thread_struct {
    Function f;
    sem_t* sucess_sem;
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

pthread_mutex_t pause_lock;
bool pause_exe;

pthread_mutex_t fd_list_lock;
FDList* fd_list;

#define SUCCESS_SEM "/success_sem"

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
    if (errno != 0) perror("");
    fprintf(stderr, "process: %d, erorr: %s\n", getpid(), msg);
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

static void* child(void* arg) {
    struct thread_struct* args = (thread_struct*) arg;
    if (args == NULL) {
        process_print("args null");
        raise(SIGKILL);
        return NULL;
    }
    sem_t* success_sem = args->sucess_sem;
    
    // copies input to p2[0]
    if (args->input != -1 && dup2(args->input, args->p2[0]) < 0)
        exit_thread("failed to copy to input pipe");

    // copies p1[1] to p2[1]
    if (dup2(args->p1[1], args->p2[1]) < 0)
        exit_thread("failed to copy to output pipe");

    // runs function stored in node
    args->f(args->p2[0], args->p2[1]); // may exit

    pthread_mutex_lock(&fd_list_lock);
    if (close(args->p1[1]) < 0) exit_thread("closing output fd");
    if (args->input != -1 && close(args->input) < 0) exit_thread("closing input fd");
    pthread_mutex_unlock(&fd_list_lock);

    return NULL;
}

static void recurse_pipeline(sem_t* success_sem, pthread_t tid[], int count, FunctionNode* node, int input) {
    if (node == NULL) return;

    // is execution paused?
    pthread_mutex_lock(&pause_lock);
    if (pause_exe) {
        fprintf(stderr, "execution paused\n");
        pthread_mutex_unlock(&pause_lock);
        raise(SIGKILL);
        return;
    }
    pthread_mutex_unlock(&pause_lock);

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

    // list of args to pass into thread
    struct thread_struct* arg = (thread_struct*)malloc(sizeof(struct thread_struct));
    arg->f = node->f;
    arg->input = input;
    arg->sucess_sem = success_sem;
    arg->p1[0] = p1[0];
    arg->p1[1] = p1[1];
    arg->p2[0] = p2[0];
    arg->p2[1] = p2[1];

    // creates new thread
    if (pthread_create(&tid[count], NULL, &child, arg) != 0) exit_recurse("pthread_create");

    recurse_pipeline(success_sem, tid, count++, node->next, p1[0]);
}

void Pipeline_execute(Pipeline* p) {
    if (p == NULL) {
        fprintf(stderr, "pipeline null\n");
        return;
    }

    // vars declared here to avoid jump issues
    pthread_t tid[p->length];

    // for signals
    int signum = 0;
    sigset_t sig_set;
    int sig;

    // process pids
    pid_t main_pid;
    pid_t container_pid;

    if (pthread_mutex_init(&pause_lock, NULL) < 0) exit_main("pause_lock");
    if (pthread_mutex_init(&fd_list_lock, NULL) < 0) exit_main("fd_list_lock");

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

    main_pid = fork();
    if (main_pid < 0) exit_main("main fork");
    else if (main_pid == 0) {
        
        /* 
        * semamphore used to determine whether the container process (process which handles all threads) and all threads were successful
        * 1 is the success state
        * 0 is the failure state
        */ 
        sem_t* success_sem = sem_open(SUCCESS_SEM, O_CREAT, 0644, 0);

        container_pid = fork();
        if (container_pid < 0) {
            process_print("container fork");
            raise(SIGKILL);
            return;
        }

        if (container_pid == 0) {
            sem_t* child_success_sem = sem_open(SUCCESS_SEM, 0);

            recurse_pipeline(child_success_sem, tid, 0, p->start_node, -1); // start pipeline execution

            // wait for running threads to terminate
            for (int i = 0; i < p->length; i++) {
                pthread_join(tid[i], NULL); 
            }
            
            sem_post(child_success_sem);
            sem_close(child_success_sem);
            exit(EXIT_SUCCESS);
            
        } else {

            // wait for child to finish
            int status;
            int pid = wait(&status);
            if (pid < 0) exit_container_parent("wait");

            // if process didn't exit with exit() or _exit
            if (!WIFEXITED(status)) exit_container_parent("child process exited abnormally");

            // if child process exited in failure            
            int success;
            if (sem_getvalue(success_sem, &success) < 0 || success != 1 || WEXITSTATUS(status) != EXIT_SUCCESS)
                exit_container_parent("container child failed");

            // exited successfully
            sem_close(success_sem);
            kill(getppid(), EXIT_SIG);
            return;
        }

    } else {

        // wait on all processes to finish, or for the exit signal
        signum = sigwait(&sig_set, &sig);
        if (signum == 0) {
            if (sig == FAIL_SIG) fprintf(stderr, "process failed\n");
        } else {
            fprintf(stderr, "sigwait failed\n");
            kill(1, SIGTERM); // kills all children, just to make sure
        }

        end:
            pthread_mutex_destroy(&pause_lock);
            pthread_mutex_destroy(&fd_list_lock);
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