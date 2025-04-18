/*
* Pipeline.h
*
*/

#ifndef PIPELINE_H_
#define PIPELINE_H_

#include <stdbool.h>

typedef void (*Function)(int input, int output);

typedef struct pipeline Pipeline;

/*
* Creates a new empty processing pipeline.
* Returns null if a processing pipeline wasn't able to be instansiated.
*/
Pipeline* new_Pipeline();

/*
* Adds the specified function to the pipeline.
* Returns true on success and false if unable to add the new stage.
*/
bool Pipeline_add(Pipeline* p, Function func);

/*
* Executes the specified pipeline.
* Will halt execution if something goes wrong, or pipeline is null.
* Halted execution may mean that some processes will continue, since they were started before execution is halted.
* Sets exit code to -1 if error.
*/
void Pipeline_execute(Pipeline* p);

/*
* Frees memory associated with the pipeline.
*/
void Pipeline_free(Pipeline* p);

#endif /* PIPELINE_H_ */
