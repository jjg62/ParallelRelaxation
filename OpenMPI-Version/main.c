#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "mpi.h"

//Redefine min and max micros to ensure they work with all compilers
#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

double startTime;

//Change this function to change the input array. This function should return the desired element in input[i][j].
double input(int i, int j) {

	//1 if in first row/column, 0 otherwise.
	if (i*j == 0) return 1;
	else return 0;
	
}

//Generate an 'unwrapped' 2D array
//Returned array needs to be freed at some point each time this is used.
double * generateArray(int arraySize) {

	double * inputUnwrapped = malloc(sizeof(double) * (size_t)(arraySize * arraySize)); //Create a 1D array
	for (int i = 0; i < arraySize; i++) {
		for (int j = 0; j < arraySize; j++) {
			inputUnwrapped[i*arraySize + j] = input(i, j); //Get element in each position from input() function.
		}
	}
	
	//Remember to free the returned array at some point
	return inputUnwrapped;

}

//Use this to access an 'unwrapped' 2D array like a regular 2D one.
double accessUnwrappedArray(double * from, int size, int i, int j) {
	return from[i * size + j];
}

//Print an 'unwrapped' 2D array
void printUnwrappedArray(double * from, int size) {
	for (int i = 0; i < size; i++) {
		for (int j = 0; j < size; j++) {
			printf("%10.6f ", from[i*size + j]);
		}
		printf("\n");
	}
	printf("\n");
}

void distributeWork(int ** chunkSizes, int ** startPositions, int arraySize, int numberOfProcessors) {

	//Divide number of elements by number of processors to get base number of tasks
	int tasksPerProcessor = arraySize * arraySize / numberOfProcessors;
	//Get remainder as well - the first r processors will get an extra element each to process.
	int r = (arraySize * arraySize) % numberOfProcessors;

	//Allocate space for arrays
	*chunkSizes = malloc(sizeof(int) * (size_t)numberOfProcessors);
	*startPositions = malloc(sizeof(int) * (size_t)numberOfProcessors);

	int startPos; //Used to keep track of where each processor starts
	startPos = 0;
	for (int i = 0; i < numberOfProcessors; i++) {
		//Work out number of elements for this processor
		int numberOfTasks = tasksPerProcessor;
		if (r > 0) { //If there is some remainder left, give an extra element to this processor
			r--;
			numberOfTasks++;
		}
		//r will always reach 0 as number of processors > r.

		//Store chunk size and start position for processor i.
		(*chunkSizes)[i] = numberOfTasks;
		(*startPositions)[i] = startPos;

		startPos += numberOfTasks; //Next processor starts where current one ends.
	}
}

void run(int rank, int arraySize, double precision, bool print) {

	//Generate initial array
	double * result = generateArray(arraySize);

	//Get number of processors
	int numberOfProcessors;
	MPI_Comm_size(MPI_COMM_WORLD, &numberOfProcessors);

	//Arrays that have size equal to #processors, storing each processor's start position and size of array to process.
	int * chunkSizes;
	int * startPositions;

	//Get information on how work is split between processors
	distributeWork(&chunkSizes, &startPositions, arraySize, numberOfProcessors);

	//Find start and end for this processor
	int startPos = startPositions[rank];
	int endPos = startPos + chunkSizes[rank] - 1;

	//May want to print which elements each processor is handling (inclusive)
	printf("Processor %d: %d-%d\n", rank, startPos, endPos);
	bool changed; //Keep track of whether a change greater than the precision has occured

	//Create a new array for this chunk's results
	double * subResults = malloc(sizeof(double) * (size_t)(endPos - startPos + 1)); 

	//Start timing after array is created
	startTime = MPI_Wtime();
	
	//Main Loop
	do {
		
		if (rank == 0) {
			//Print the result once before each iteration.
			if (print) {
				printUnwrappedArray(result, arraySize);
			}
			
			//Send relevant part of array to each other processor
			//(In a previous version I had sent the whole array to each processor, however this had dramatic negative effect on speed for very large arrays)
			for (int i = 1; i < numberOfProcessors; i++) {

				//Calculate the rows of the matrix that process i will need
				//These are: the whole rows which contain the relevant elements, the row above and the row below
				int startingRow = max(0, (startPositions[i] / arraySize) - 1);
				int endRow = min(arraySize-1, ((startPositions[i] + chunkSizes[i] - 1) / arraySize) + 1);

				//Send those rows to processor i
				MPI_Send(result + startingRow * arraySize, (endRow-startingRow+1)*arraySize, MPI_DOUBLE, i, 0, MPI_COMM_WORLD);

			}

		}
		else {

			//Calculate the rows needed for this processor to do its calculations
			//These are: the whole rows which contain the relevant elements, the row above and the row below
			int startingRow = max(0, (startPos / arraySize) - 1);
			int endRow = min(arraySize - 1, (endPos / arraySize) + 1);

			//Read those rows into the result array (used to do the calculations)
			//Note that, the result array will only display the full correct result in processor 0. To save time, the other processors
			//only correctly store the elements they will work on.
			MPI_Recv(result+startingRow*arraySize, (endRow-startingRow+1)*arraySize, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		}
		
		
		changed = false; //changed false by default
		for (int i = 0; i <= endPos - startPos; i++) {
			//Check if element is on edge
			int row = (startPos + i) / arraySize;
			int col = (startPos + i) % arraySize;
			bool onEdge = row == 0 || row == arraySize - 1 || col == 0 || col == arraySize - 1;

			//Copy element if on edge of array
			if (onEdge) {
				subResults[i] = result[startPos + i];
			}else {
				//Otherwise calculate average of 4 adjacent elements
				double adjacentSum = accessUnwrappedArray(result, arraySize, row-1, col) +
									accessUnwrappedArray(result, arraySize, row+1, col) +
									accessUnwrappedArray(result, arraySize, row, col-1) +
									accessUnwrappedArray(result, arraySize, row, col+1);

				subResults[i] = (double)adjacentSum / (double)4.0;

				//If a change greater than precision occurred, update changed
				changed = changed || fabs(subResults[i] - result[startPos + i]) > precision;
			}
		}

		//Gather v: Combines subresults of each processor and gives result to rank 0.
		//The 'v' means that the subresults can each have varied sizes, which is why each processor needs chunkSizes and startPositions arrays.
		//This function is blocking, so each processor will not proceed until they send their subResults array
		//and processor 0 won't try to calculate result until each other processor has copied their subResults out of their send buffers
		//So this won't cause races.
		MPI_Gatherv(subResults, endPos-startPos+1, MPI_DOUBLE, result, chunkSizes, startPositions, MPI_DOUBLE, 0, MPI_COMM_WORLD);

		//All reduce: Updates changed to be the result of applying || to each processor's value of changed, and sends the new version to all processors.
		//Similarly to Gatherv, this is blocking and so races won't occur
		//Need to use 'MPI_IN_PLACE' to safely be able to use the same send and receive buffer (we are updating the value of changed using its current value!)
		MPI_Allreduce(MPI_IN_PLACE, &changed, 1, MPI_C_BOOL, MPI_LOR, MPI_COMM_WORLD);
		
	}while(changed); //Keep iterating until no processor get changed = true

	//Print the final result (once)
	if (print && rank == 0) {
		printUnwrappedArray(result, arraySize);
	}

	//Free arrays
	free(subResults);
	free(result);
	free(chunkSizes);
	free(startPositions);

}



int main(int argc, char* argv[]) {


	MPI_Init(&argc, &argv); //Setup MPI
	//Get rank of this processor
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	//Arguments - feel free to edit these.
	int arraySize = 8; //To change contents of array, edit the input() function.
	double precision = 0.0001;
	bool print = true;

	run(rank, arraySize, precision, print);

	//Print time from after array construction
	if(rank == 0) printf("\n\nTime taken: %10.6f seconds", (double)MPI_Wtime() - startTime);

	MPI_Finalize(); //Make sure MPI exits propely

	

	return 0;
}