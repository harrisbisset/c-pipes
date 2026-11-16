CC = clang
RM = rm -f
DFLAG = -g
GFLAGS = -Wall -Wextra -pthread -I.
CFLAGS = $(DFLAG) $(GFLAGS) -c
LFLAGS = $(DFLAG) $(GFLAGS)
SANFLAGS = -g -O1 -fno-omit-frame-pointer -fsanitize=undefined -fno-sanitize-recover=undefined

.PHONY: all test test-ubsan clean TestPipeline SumSquaresPipeline SievePipeline

all: SumSquaresPipeline SievePipeline TestPipeline


test: TestPipeline
	timeout -k 1s 10s ./build/TestPipeline

test-ubsan: build/ubsan/TestPipeline
	UBSAN_OPTIONS="print_stacktrace=1" timeout -k 1s 10s ./build/ubsan/TestPipeline


TestPipeline: build/TestPipeline

SumSquaresPipeline: build/SumSquaresPipeline

SievePipeline: build/SievePipeline


build/TestPipeline: build/TestPipeline.o build/Pipeline.o
	$(CC) $(LFLAGS) build/TestPipeline.o build/Pipeline.o -o build/TestPipeline

build/SumSquaresPipeline: build/SumSquaresPipeline.o build/Pipeline.o
	$(CC) $(LFLAGS) build/SumSquaresPipeline.o build/Pipeline.o -o build/SumSquaresPipeline

build/SievePipeline: build/SievePipeline.o build/Pipeline.o
	$(CC) $(LFLAGS) build/SievePipeline.o build/Pipeline.o -o build/SievePipeline

build/ubsan/TestPipeline: build/ubsan/TestPipeline.o build/ubsan/Pipeline.o
	$(CC) $(SANFLAGS) $(LFLAGS) build/ubsan/TestPipeline.o build/ubsan/Pipeline.o -o build/ubsan/TestPipeline


build/TestPipeline.o: TestPipeline.c Pipeline.h PipelineHelper.h Makefile | build
	$(CC) $(CFLAGS) TestPipeline.c -o build/TestPipeline.o

build/SievePipeline.o: examples/SievePipeline.c Pipeline.h Makefile | build
	$(CC) $(CFLAGS) examples/SievePipeline.c -o build/SievePipeline.o

build/SumSquaresPipeline.o: examples/SumSquaresPipeline.c Pipeline.h Makefile | build
	$(CC) $(CFLAGS) examples/SumSquaresPipeline.c -o build/SumSquaresPipeline.o

build/Pipeline.o: Pipeline.c Pipeline.h Makefile | build
	$(CC) $(CFLAGS) Pipeline.c -o build/Pipeline.o

build/ubsan/Pipeline.o: Pipeline.c Pipeline.h Makefile | build/ubsan
	$(CC) $(SANFLAGS) $(CFLAGS) Pipeline.c -o build/ubsan/Pipeline.o

build/ubsan/TestPipeline.o: TestPipeline.c Pipeline.h PipelineHelper.h Makefile | build/ubsan
	$(CC) $(SANFLAGS) $(CFLAGS) TestPipeline.c -o build/ubsan/TestPipeline.o


clean:
	$(RM) build/SumSquaresPipeline build/SievePipeline build/TestPipeline build/*.o
	$(RM) build/ubsan/TestPipeline build/ubsan/*.o
