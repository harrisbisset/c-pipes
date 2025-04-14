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
* Returns null, if a processing pipeline wasn't able to be instansiated, and prints to stderr.
*/
Pipeline* new_Pipeline();

/*
* Adds the specified function to the pipeline.
* Returns true on success and false if unable to add the new stage.
* Will print to stderr if false is returned.
*/
bool Pipeline_add(Pipeline* this, Function f);

/*
* Executes the specified pipeline.
* Will halt execution and print to stderr if soemthing goes wrong, or pipeline is null.
*/
void Pipeline_execute(Pipeline* this);


/*
* Frees memory associated with the pipeline.
* Will print to stderr if pipeline is null.
*/
void Pipeline_free(Pipeline* this);

#endif /* PIPELINE_H_ */
