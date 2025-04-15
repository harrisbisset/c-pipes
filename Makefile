CC = clang
RM = rm -f
DFLAG = -g
GFLAGS = -Wall -Wextra
CFLAGS = $(DFLAG) $(GFLAGS) -c
LFLAGS = $(DFLAG) $(GFLAGS)

run: clean SievePipeline SumSquaresPipeline start

start:
	- killall -s 9 SievePipeline
	./SievePipeline < sieve1.in


all: SumSquaresPipeline SievePipeline

SumSquaresPipeline: SumSquaresPipeline.o Pipeline.o 
	$(CC) $(LFLAGS) SumSquaresPipeline.o Pipeline.o -o SumSquaresPipeline

SievePipeline: SievePipeline.o Pipeline.o 
	$(CC) $(LFLAGS) SievePipeline.o Pipeline.o -o SievePipeline


SievePipeline.o: SievePipeline.c
	$(CC) $(CFLAGS) SievePipeline.c

SumSquaresPipeline.o: SumSquaresPipeline.c
	$(CC) $(CFLAGS) SumSquaresPipeline.c


Pipeline.o: Pipeline.c Pipeline.h
	$(CC) $(CFLAGS) Pipeline.c


clean:
	$(RM) SumSquaresPipeline SievePipeline *.o
