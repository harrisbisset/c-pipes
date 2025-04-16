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
* Returns null if a processing pipeline wasn't able to be instansiated and prints to stderr.
*/
Pipeline* new_Pipeline();

/*
* Adds the specified function to the pipeline.
* Returns true on success and false if unable to add the new stage.
* Will print the erorr to stderr if false is returned.
*/
bool Pipeline_add(Pipeline*, Function);

/*
* Executes the specified pipeline.
* Will halt execution and print to stderr if soemthing goes wrong, or pipeline is null.
* Halted execution may mean that some processes will continue, since they were started before execution is halted.
*/
void Pipeline_execute(Pipeline*);


/*
* Frees memory associated with the pipeline.
* Will print to stderr if pipeline is null.
*/
void Pipeline_free(Pipeline*);

#endif /* PIPELINE_H_ */
