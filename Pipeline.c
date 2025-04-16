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
#include <string.h>

#include "Pipeline.h"

// used to check whether an error value is returned from a function
#define catch_perr(result, child, msg) ({                           \
    if (result) {                                                   \
        if (errno != 0) perror(msg);                                \
        if (child) exit(EXIT_FAILURE);                              \
        else {                                                      \
            if (errno == 0) fprintf(stderr, "error: %s\n", msg);    \
            error_code = Other;                                     \
            raise(FAIL_SIG);                                        \
            return;                                                 \
        }                                                           \
    }                                                               \
})                                                                  

// used to check whether a value to null, when it shouldn't be
#define check_null(val, msg, ret) ({                \
    if (val == NULL) {                              \
        if (errno != 0) perror(msg);                \
        else fprintf(stderr, "%s\n", msg);          \
        return ret;                                 \
    }                                               \
})                                                  

enum ERR_CODE {
    Other = -1, // don't print error_code
    Default = 0, // the default value when not set
    Abnormal = 1, // abnormal termination
    FunctionFailedInExit = 2, // function set perror, and exited
    ExitFailure = 3, // function exited not in success
    FunctionFailedNoExit = 4 // function set perorr, and didn't exit
};

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

enum ERR_CODE error_code = Default;
int session = 0;
int function_success = 0; // used to check whether a function caused the child process to fail
sem_t pnum; // used to count the child processes

static const char* str_err(enum ERR_CODE c) {
    if (c == Abnormal) {
        return "child process exited abnormally";
    } else if (c == FunctionFailedInExit) {
        return "function caused child process to fail";
    } else if (c == ExitFailure) {
        return "child process exited in failure";
    } else if (c == FunctionFailedNoExit) {
        return strerror(errno);
    } else if (c == Other || c == Default) {
        return "didn't expect to print error_code";
    }

    return "err_code not implemented";
}

Pipeline* new_Pipeline() {
    Pipeline* p = (Pipeline*)malloc(sizeof(Pipeline));
    check_null(p, "pipeline not able to be instansiated", NULL);
    p->start_node = NULL;
    p->end_node = NULL;
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
    // something has gone wrong, likely user has modified end_node
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
static void child_handler(int sig __attribute__((unused)), siginfo_t *info __attribute__((unused)), void *context __attribute__((unused))) {

    while (1) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) {
            break;
        }
        // if process exited with exit() or _exit
        if (!WIFEXITED(status)) {
            error_code = Abnormal;
            raise(FAIL_SIG);
            return;
        }
        // if child process exited in failure
        if (WEXITSTATUS(status) != EXIT_SUCCESS) {
            if (!function_success) error_code = FunctionFailedInExit; // if function caused child process to fail
            else error_code = ExitFailure;
            raise(FAIL_SIG);
            return;
        }
        sem_trywait(&pnum);
    }

    // err(close(p1[1]), 0, "closing output fd");
    // if (input != -1) err(close(input), 0, "closing input fd");
    
    // if last child terminated, raise recurse exit
    int pnum_val;
    sem_getvalue(&pnum, &pnum_val);
    if (pnum_val <= 0) {
        raise(EXIT_SIG);
        return;
    }
}

static void recurse_pipeline(FunctionNode* node, int input) {
    if (node == NULL) return;

    int p1[2];
    int p2[2];

    catch_perr(pipe(p1) < 0, false, "creating pipeline");
    catch_perr(pipe(p2) < 0, false, "creating pipeline");

    struct sigaction act;
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = &child_handler;
    catch_perr(sigaction(SIGCHLD, &act, NULL) < 0, false, "sigaction faild");

    pid_t pid;
    pid = fork();
    catch_perr(pid < 0, false, "creating a fork");

    // if fork() == 0, then we are in the child process
    if (pid == 0) {
        catch_perr(close(p1[0]) < 0, true, "closing fd"); // unused, since we take the last input as the input pipe

        if (input != -1) catch_perr(dup2(input, p2[0]) < 0, true, "failed to copy to input pipe");
        catch_perr(dup2(p1[1], p2[1]) < 0, true, "failed to copy to output pipe");

        function_success = 0;
        node->f(p2[0], p2[1]);
        function_success = 1;

        // if the function set an error, but didn't exit
        if (errno != 0) {
            error_code = FunctionFailedNoExit;
            exit(EXIT_FAILURE);
        };

        exit(EXIT_SUCCESS);

    // fork() > 0, then we are in the parent process
    } else {
        recurse_pipeline(node->next, p1[0]);
    }

    // close file descriptors if any still open
    // if (input != -1) close(input);
    // close(p1[0]);
    // close(p1[1]);
    // close(p2[0]);
    // close(p2[1]);
}

void Pipeline_execute(Pipeline* p) {
    if (p == NULL) {
        fprintf(stderr, "pipeline null");
        return;
    }

    // create exit signal, which the program will wait for
    sigset_t sig_set;
    int sig;
    sigemptyset(&sig_set);
    sigaddset(&sig_set, EXIT_SIG);
    sigaddset(&sig_set, FAIL_SIG);
    catch_perr(sigprocmask(SIG_BLOCK, &sig_set, NULL) < 0, false, "mask failed");    

    // create semaphores to handle shared memory
    catch_perr(sem_init(&pnum, 1, p->length) < 0, false, "pnum semaphore init");
    
    error_code = Default;
    recurse_pipeline(p->start_node, -1); // start pipeline execution

    // wait on all processes to finish
    int signum = sigwait(&sig_set, &sig);
    catch_perr(sem_destroy(&pnum) < 0, false, "failed to destroy pnum semaphore");
    if (signum == 0) {
        if (sig == FAIL_SIG) {
            if (error_code != Other) fprintf(stderr, "%s\n", str_err(error_code));
            kill(1, SIGTERM); // kills all children
        }
    } else {
        fprintf(stderr, "sigwait failed");
        kill(1, SIGTERM); // kills all children, just to make sure
    }
}

void Pipeline_free(Pipeline* p) {
    if (p == NULL) {
        fprintf(stderr, "pipeline null");
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