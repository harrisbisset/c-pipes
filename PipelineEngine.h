/*
 * Pipeline.h
 *
 */

#ifndef PIPELINE_H_
#define PIPELINE_H_

#include <stdbool.h>
#include <pthread.h>

typedef void (*Function)(int input, int output);

typedef struct Engine {
    pthread_mutex_t mutex;
    int last_pid;
    struct PipelineNode* node_start;
    struct PipelineNode* node_end;
} Engine;

typedef struct PipelineNode {
    struct PipelineNode* next;
    struct Pipeline* pipeline;
} PipelineNode;

typedef struct Pipeline {
    const char* name;
    int pid;
    Function* function;
} Pipeline;

Engine* NewEngine();

/*
* 
*/
int SpawnPipeline(Engine* e);

/**
* 
*/
void PipelineAdd(Engine* e, int pid, Function f);

/*
* Adds the specified function to the pipeline.
* Returns true on success and false if unable to add the new stage.
*/
void PipelineSend(Pipeline* this);

/*
* Frees memory associated with the pipeline.
*/
void PipelineFree(Pipeline* this);

#endif /* PIPELINE_H_ */
