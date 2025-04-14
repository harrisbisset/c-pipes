void cleanupExit(Pipeline *p) {
    Pipeline_free(p);
    exit(-1);
}