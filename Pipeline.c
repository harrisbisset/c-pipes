/*
* Pipeline.c
*
*/

#define _XOPEN_SOURCE 700
#define _GNU_SOURCE // for asprintf

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h> 
#include <fcntl.h>
#include <string.h>
#include <semaphore.h>

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
    free(args);                                     \
    free(arg);                                      \
    exit(EXIT_FAILURE);                             \
    return NULL;                                    \
})

// used to exit from the parent process in recurse_pipeline
#define exit_recurse(msg) ({                \
    process_print(msg);                     \
    exit(EXIT_FAILURE);                     \
    return;                                 \
})

// used to exit from the main process before we start the recursion
#define exit_main(msg) ({                   \
    p->exit_code = -1;                      \
    process_print(msg);                     \
    goto end;                               \
})

// used to exit from the container parent process if the child fails
#define exit_container_parent(msg) ({                                                       \
    int exit_perror = errno;                                                                \
    if (sem_close(exit_sem) < 0) if (debug) perror("close exit_sem");                       \
    if (sem_unlink(exit_sem_name) < 0) if (debug) perror("close exit_sem");                 \
    if (debug && exit_perror != 0)                                                          \
        fprintf(stderr, "process: %i, %s: %s\n", getpid(), msg, strerror(exit_perror));     \
    else if (debug) fprintf(stderr, "process: %i, %s\n", getpid(), msg);                    \
    exit(EXIT_FAILURE);                                                                     \
    return;                                                                                 \
})

typedef struct thread_struct {
    Function f;
    int debug;
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
    int debug;
    int exit_code;
    FunctionNode* start_node;
    FunctionNode* end_node;
};

#define max_threads 6

pthread_mutex_t pause_lock;
bool pause_exe;

pthread_mutex_t fd_list_lock;
FDList* fd_list = NULL;

static long pipeline_counter;

Pipeline* new_Pipeline() {
    Pipeline* p = (Pipeline*)malloc(sizeof(Pipeline));
    check_null(p, "pipeline not able to be instansiated", NULL);
    p->start_node = NULL;
    p->end_node = NULL;
    p->length = 0;
    p->debug = 0;
    p->exit_code = 0;
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
        exit(EXIT_FAILURE);
        return NULL;
    }
    int debug = args->debug;

    if (debug) fprintf(stderr, "starting thread: %lu\n", pthread_self());

    // these should always be valid file descriptors    
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

    pthread_mutex_lock(&fd_list_lock);
    if (close(args->p1[1]) < 0) exit_thread("closing output fd");
    if (args->input != -1 && close(args->input) < 0) exit_thread("closing input fd");
    pthread_mutex_unlock(&fd_list_lock);

    if (debug) fprintf(stderr, "exiting thread: %lu\n", pthread_self());
    
    free(args);
    return NULL;
}

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

    FDNode* temp;
    FDNode* head = list->start_node;
    while (head != NULL) {
       temp = head;
       head = head->next;
       close(temp->p1_input);
       close(temp->p1_output);
       close(temp->p2_input);
       close(temp->p2_output);
       free(temp);
    }

    free(list);
}

static void recurse_pipeline(pthread_t tid[], int count, int debug, FunctionNode* node, int input) {
    if (node == NULL) return;

    // is execution paused?
    pthread_mutex_lock(&pause_lock);
    if (pause_exe) {
        fprintf(stderr, "execution paused\n");
        pthread_mutex_unlock(&pause_lock);
        exit(EXIT_FAILURE);
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

    int debug = p->debug;

    // vars declared here to avoid jump issues
    pthread_t tid[p->length];

    // process pids
    pid_t main_pid;
    pid_t container_pid;

    // dynamic name, as pipeline may spawn pipeline children which use the same name
    pipeline_counter++;
    char* exit_sem_name = NULL;
    if (asprintf(&exit_sem_name, "/exit_sem_%ld", pipeline_counter) < 0)
        exit_main("asprintf exit_sem_name");
    
    sem_unlink(exit_sem_name); // cleanup if still open, ignore error

    if (pthread_mutex_init(&pause_lock, NULL) < 0) exit_main("pause_lock");
    if (pthread_mutex_init(&fd_list_lock, NULL) < 0) exit_main("fd_list_lock");

    // creates linked list to store file descriptors
    fd_list = (FDList*)malloc(sizeof(FDList));
    if (fd_list == NULL) exit_main("malloc fdlist");
    fd_list->start_node = NULL;
    fd_list->end_node = NULL;

    main_pid = fork();
    if (main_pid < 0) exit_main("main fork");

    // main child process
    else if (main_pid == 0) {
        if (debug) fprintf(stderr, "parent: %d, main child: %d\n", getppid(), getpid());

        sem_t* exit_sem = sem_open(exit_sem_name, O_CREAT, 0644, 0);

        container_pid = fork();
        if (container_pid < 0) {
            process_print("container fork");
            exit(EXIT_FAILURE);
            return;
        }

        // container child process
        if (container_pid == 0) {

            if (debug) fprintf(stderr, "parent: %d, container child: %d\n", getppid(), getpid());
            recurse_pipeline(tid, 0, debug, p->start_node, -1); // start pipeline execution

            // wait for running threads to terminate
            for (int i = 0; i < p->length; i++) {
                if (debug) fprintf(stderr, "parent %i: waiting on %dth thread\n", getppid(), i);
                if (pthread_join(tid[i], NULL) != 0) {
                    if (debug) fprintf(stderr, "terminated on %dth thread\n", i);
                }
            }

            // update success
            sem_t* child_exit_sem = sem_open(exit_sem_name, 0);
            sem_post(child_exit_sem);
            sem_close(child_exit_sem);

            exit(EXIT_SUCCESS);
            return;
            
        // container parent process
        } else {
            // wait for child to finish
            int status;
            int np = waitpid(container_pid, &status, 0);
            if (np < 0) exit_container_parent("waitpid failed");

            if (debug) fprintf(stderr, "parent: %d, container parent: %d\n", getppid(), getpid());

            // if process didn't exit with exit() or _exit
            if (!WIFEXITED(status))
                exit_container_parent("container child exited abnormally");
            
            // check if terminated correctly
            int exit_status;
            if (sem_getvalue(exit_sem, &exit_status) < 0)
                exit_container_parent("failed reading exit_sem");
            

            // if child process exited in failure
            if (WEXITSTATUS(status) != EXIT_SUCCESS || exit_status != 1)
                exit_container_parent("container child failed");

            sem_close(exit_sem);
            sem_unlink(exit_sem_name);
            exit(EXIT_SUCCESS);
            return;
        }

    // main parent process
    } else {
        // wait for child to finish
        int status;
        if (waitpid(main_pid, &status, 0) < 0) exit_main("wait");
        if (debug) fprintf(stderr, "parent: %d, main parent: %d\n", getppid(), getpid());

        // if process didn't exit with exit() or _exit
        if (!WIFEXITED(status)) exit_main("container process exited abnormally");

        // if child process exited in failure
        if (WEXITSTATUS(status) != EXIT_SUCCESS) exit_main("container parent failed");
        
        p->exit_code = 0; // success

        end:
            sem_unlink(exit_sem_name); // just to make sure unlinked
            free(exit_sem_name);

            pthread_mutex_lock(&fd_list_lock);
            FDList_free(fd_list); // close all open pipes
            pthread_mutex_unlock(&fd_list_lock);

            pthread_mutex_destroy(&pause_lock);
            pthread_mutex_destroy(&fd_list_lock);
        
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