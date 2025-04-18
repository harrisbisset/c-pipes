/*
* PipelineHelper.h
*
*/

#ifndef PIPELINE_HELPER_H_
#define PIPELINE_HELPER_H_

#include "Pipeline.h"

// used to det the debug field
// 1 is for SHOW DEBUG
// 2 is for HIDE DEBUG
// always printed to stderr
void Pipeline_setDebug(Pipeline* p, int debug);

// used to get the exit code from the pipeline
// -3 is for not executed yet
// -2 is for NULL
// -1 is for failure
// 0 is for success
int Pipeline_exitCode(Pipeline* p);

#endif /* PIPELINE_HELPER */