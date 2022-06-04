#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

//Global variables
bool changed;
bool finished;
float arrayConstructTime;

pthread_barrier_t barrier;

//Struct used to pass multiple arguments to threadMain
struct threadMainArgs{
    double ** resultArray;
    double ** startArray;
    int arrayWidth;
    int arrayHeight;
    int startPos;
    int elementsToCompute;

    double precision;
    bool * changed;

};

//Returns the equivalent of array[i][j]
double accessUnwrapped2DArray(double* unwrappedArray, int width, int i, int j){
    return unwrappedArray[i*width + j];
}

void* threadMain(void* arg){

    //Cast argument to pointer to struct
    struct threadMainArgs * args = (struct threadMainArgs*) arg;

    //Extract arguments from struct
    double** resultArray = args->resultArray;
    double** startArray = args->startArray;
    int startPos = args->startPos;
    int elementsToCompute = args->elementsToCompute;
    int arrayWidth = args->arrayWidth;
    int arrayHeight = args->arrayHeight;
    double precision = args->precision;
    bool * changed = args->changed;

    //Sync first iteration with the main while loop
    pthread_barrier_wait(&barrier);

    do{

        double * currentStartArray = *startArray;
        double * currentResultArray = *resultArray;

        //Process this thread's share of elements
        for(int i = startPos; i < startPos + elementsToCompute; i++){
            bool edge = i < arrayWidth || i >= arrayWidth*(arrayHeight-1) || //Top or bottom
                i % arrayWidth == 0 || i % arrayWidth == arrayWidth-1; //Left or right

            if(edge){
                currentResultArray[i] = currentStartArray[i];
            }else{
                double total = currentStartArray[i-arrayWidth] + currentStartArray[i+arrayWidth] +
                    currentStartArray[i-1] + currentStartArray[i+1];

                currentResultArray[i] = total / 4.0;
                if(currentResultArray[i] - currentStartArray[i] >= precision){
                    *changed = true;
                }
            }
        }


        pthread_barrier_wait(&barrier);
        //Sequential part (main thread) is ran
        pthread_barrier_wait(&barrier);

    }while(!finished);

    pthread_exit(0);
	return;
}

void solve(int size, double inputArray[size][size], int threadCount, double precision, bool print){
    changed = true;

    //Unwrap input array into 1D format
    double* startArray = malloc(size*size*sizeof(double));
    if(startArray == NULL){
        printf("ERROR: Couldn't allocate memory for startArray");
        return;
    }

    for(int x = 0; x < size; x++){
        for(int y = 0; y < size; y++){
            startArray[x*size + y] = inputArray[x][y];
        }
    }

	//Used to calculate how many elements to give each thread
    int elementsEachThread = (size*size) / threadCount;
    int remainderElements, currentPos;

    pthread_t threadIds[threadCount];

    double* resultArray;

    if(pthread_barrier_init(&barrier, NULL, threadCount+1) != 0){
		printf("ERROR: Barrier could not be initialised");
	}

    //Generate threads
    struct threadMainArgs threadArgs[threadCount];
    remainderElements = (size*size) % threadCount;
    currentPos = 0;

    for(int i = 0; i < threadCount; i++){

		//Get arguments to pass to thread
        threadArgs[i].resultArray = &resultArray;
        threadArgs[i].startArray = &startArray;
        threadArgs[i].startPos = currentPos;
        threadArgs[i].elementsToCompute = elementsEachThread;
        if(remainderElements > 0){
            //Distribute remainder between threads
            threadArgs[i].elementsToCompute++;
            remainderElements--;
        }
        threadArgs[i].arrayWidth = size;
        threadArgs[i].arrayHeight = size;
        threadArgs[i].precision = precision;
        threadArgs[i].changed = &changed;

		//Create the threads and start running threadMain on them
        pthread_create(&threadIds[i], NULL, threadMain, &threadArgs[i]);

        currentPos += threadArgs[i].elementsToCompute; //Keep track of current index in the array
    }

    finished = false;

    do{
        //This part is ran sequentially
		//Make a new result array
        resultArray = malloc(size*size*sizeof(double));
        if(resultArray == NULL){
            printf("ERROR: Couldn't allocate memory for resultArray");
            return;
        }
        changed = false;

        pthread_barrier_wait(&barrier);

        //Other threads do one iteration

        pthread_barrier_wait(&barrier);

        //Back to sequential
        free(startArray);
        startArray = resultArray; //current resultArray becomes next iteration's startArray
		
		//Print array if that option is turned on
		if(print){
			printf("[");
			for(int x = 0; x < size; x++){
				for(int y = 0; y < size; y++){
					printf("%f ", accessUnwrapped2DArray(resultArray, size, x, y));
				}
				printf("\n ");
			}
			printf("]\n\n");
		}

    }while(changed);

    //If changed=true, above loop ends but threads are still waiting on their last barrier
    //So let them end by changing finished and waiting once more
    finished = true;
    pthread_barrier_wait(&barrier);

    //Join threads
    for(int i = 0; i < threadCount; i++){
        pthread_join(threadIds[i], NULL);
    }

	//Clean up
    free(startArray);
    pthread_barrier_destroy(&barrier);
    printf("Finished Successfully!\n");
}

int main(int argc, char *argv[]) {
	//Check that valid arguments have been given
    if(argc !=5 || atoi(argv[1]) <= 0 || atoi(argv[2]) <= 0 || atof(argv[3]) <= 0 ||
        (atoi(argv[4]) != 0 && atoi(argv[4]) != 1)){
            printf("Error: incorrect usage. Must supply command line arguments: ./program [array dimension] [thead count] [precision] [print result? 1 if yes, 0 if no]\n");
            return 0;
    }

	//Get command line arguments
    int size = atoi(argv[1]);
    int threadCount = atoi(argv[2]);
    double precision = atof(argv[3]);
    bool print = atoi(argv[4]);

    printf("Running program for array of dimension %d, with %d threads, and precision of %f...\n", size, threadCount, precision);

	//Generate random array
	srand(101121);
    double inputArray[size][size];
	printf("[");
    for(int i = 0; i<size; i++){
        for(int j = 0; j<size; j++){
			/*
            if(i == 0 || j == 0){
                inputArray[i][j] = 1;
            }else{
                inputArray[i][j] = 0;
            }
			*/
			inputArray[i][j] = rand() % 20;
        }
    }
	

	//Print array construction time such that it may be subtracted from total time to get algorithm time
    arrayConstructTime = (float)clock() / CLOCKS_PER_SEC;
    printf("Array construction took: %f.\n", arrayConstructTime);


    solve(size, inputArray, threadCount, precision, print);

    return 0;
}
