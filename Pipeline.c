/*
* Pipeline.c
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Pipeline.h"


Pipeline* new_Pipeline() {
    Pipeline* p = (Pipeline*)malloc(sizeof(Pipeline));
    p->pid = 0; // todo
    p->f_len = 0;
    p->f_pos = 0;
    p->functions = NULL;
    return p;
}


bool Pipeline_add(Pipeline* this, Function f) {
    if (this == NULL || f == NULL) return false;

    int new_len = this->f_len + 1;

    if (this->functions == NULL) {
        this->functions = (Function*)calloc(1, sizeof(Function));

        // memory not allocated
        if (this->functions == NULL) return false;
        
    } else {
        Function* temp_f = this->functions;
        this->functions = (Function*)realloc(this->functions, new_len * sizeof(this->functions));

        // memory not allocated
        if (this->functions == NULL)  {
            this->functions = temp_f;
            return false;
        }
    }

    this->functions[new_len] = f;
    this->f_len = new_len;

    return true;
}


void Pipeline_execute(Pipeline* this) {
}


void Pipeline_free(Pipeline* this) {
}

 