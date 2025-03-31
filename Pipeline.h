/*
* Pipeline.h
*
*/

#ifndef PIPELINE_H_
#define PIPELINE_H_

#include <stdbool.h>

typedef void (*Function)(int input, int output);

typedef struct FunctionNode {
    struct FunctionNode* next;
    Function f;
} FunctionNode;

typedef struct Pipeline {    
    FunctionNode* start_node;
    FunctionNode* end_node;
} Pipeline;

/*
* Creates a new empty processing pipeline.
*/
Pipeline* new_Pipeline();

/*
* Adds the specified function to the pipeline.
* Returns true on success and false if unable to add the new stage.
*
*/
bool Pipeline_add(Pipeline* this, Function f);

/*
* Executes the specified pipeline.
*/
void Pipeline_execute(Pipeline* this);


/*
* Frees memory associated with the pipeline.
*/
void Pipeline_free(Pipeline* this);

#endif /* PIPELINE_H_ */
