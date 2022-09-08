#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <mpi.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

// This program tests a mixture of normal and MPI file descriptors
int main()
{
    MPI_Init(NULL, NULL);
    MPI_File f1;
    MPI_File f3;
    MPI_Status stat;

    // Open files
    MPI_File_open(MPI_COMM_WORLD, "test1.txt",
                  MPI_MODE_CREATE|MPI_MODE_RDWR, MPI_INFO_NULL, &f1);
    int f2 = open("test2.txt", O_RDWR|O_CREAT);
    fchmod(f2, S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IWGRP
           |S_IXGRP|S_IROTH|S_IWOTH|S_IXOTH);
    MPI_File_open(MPI_COMM_WORLD, "test3.txt",
                  MPI_MODE_CREATE|MPI_MODE_RDWR, MPI_INFO_NULL, &f3);

    // Write content to each file
    char buf[1024];
    strcpy(buf, "abcd");
    fprintf(stderr, "%s\n", buf); fflush(stderr);
    MPI_File_write_at(f1, 0, buf, strlen(buf), MPI_CHAR, &stat);
    strcpy(buf, "efgh");
    fprintf(stderr, "%s\n", buf); fflush(stderr);
    write(f2, buf, strlen(buf));
    strcpy(buf, "ijkl");
    fprintf(stderr, "%s\n", buf); fflush(stderr);
    MPI_File_write_at(f3, 0, buf, strlen(buf), MPI_CHAR, &stat);
    strcpy(buf, "mnop");
    fprintf(stderr, "%s\n", buf); fflush(stderr);

    // Sleep
    fprintf(stderr, "sleeping\n"); fflush(stderr);
    sleep(10);

    // Read contents from files
    MPI_File_read_at_all(f1, 0, buf, 5, MPI_CHAR, &stat);
    fprintf(stderr, "%s\n", buf); fflush(stderr);
    lseek(f2, 0, SEEK_SET);
    int r = read(f2, buf, 5);
    fprintf(stderr, "%s\n", buf); fflush(stderr);
    MPI_File_read_at_all(f3, 0, buf, 5, MPI_CHAR, &stat);
    fprintf(stderr, "%s\n", buf); fflush(stderr);

    // Close file descriptors
    MPI_File_close(&f1);
    MPI_File_close(&f3);
    close(f2);

    MPI_Finalize();
}