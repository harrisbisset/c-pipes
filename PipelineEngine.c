/*
 * PipelineEngine.c
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "Pipeline.h"

/**
* Creates a 
*/
Engine* NewEngine() {
    Engine* engine = (Engine*)malloc(sizeof(engine));
    engine->last_pid = 0;
    engine->node_start = NULL;
    engine->node_end = NULL;
    return engine;
}

/**
* Gets the next pid for a pipeline, and increments the pid counter.
*/
int GetNextPID(Engine* engine) {
    pthread_mutex_lock(&engine->mutex);
    engine->last_pid++;
    pthread_mutex_lock(&engine->mutex);
    return engine->last_pid;
}

int SpawnPipeline(Engine* e) {
    if (e == NULL) return false;

    Pipeline* pipeline = malloc(sizeof(pipeline));
    pipeline->pid = GetNextPID(e);

    PipelineNode* node = (PipelineNode*)malloc(sizeof(PipelineNode));
    node->pipeline = pipeline;
    node->next = NULL;

    // if first pipeline in linked list
    if (e->node_start == NULL) {
        e->node_start = node;
        e->node_end = node;
    } else {
        e->node_end->next = node;
        e->node_end = e->node_end->next;
    }

    return pipeline->pid;
}

void PipelineAdd(Engine* e, int pid, Function f) {
    
}


void PipelineSend(Pipeline* this) {
    if (this == NULL) return;
}


void PipelineFree(Pipeline* this) {
    if (!this) return;
    free(this);
}