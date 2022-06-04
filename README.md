# Parallel Relaxation
A basic relaxation algorithm, parallelised in two different ways - with POSIX C Primitves, and with OpenMPI.
For detailed analysis of correctness and scalability, see the accompanying pdf files in each folder.

## Usage - POSIX

The POSIX version can be compiled normally using the lpthread library, for example:

gcc main.c -o main -lpthread

To run it, use the following:

./main [array dimension (int)] [thread count (int)] [precision (double)] [print (1 = yes, 0 = no)]

## Usage - OpenMPI

The OpenMPI version must be compiled using an appropriate compiler, for example mpicc, using the following command:

mpicc -Wall -Wextra -Wconversion -o main main.c

The given executable then must be ran using mpirun or mpiexec.
