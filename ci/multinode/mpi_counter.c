#define _POSIX_C_SOURCE 200809L

#include <mpi.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { PATH_CAPACITY = 4096 };

static void mpi_fail(int error_code, const char *operation)
{
    char error_text[MPI_MAX_ERROR_STRING];
    int error_length = 0;

    MPI_Error_string(error_code, error_text, &error_length);
    fprintf(stderr, "%s failed: %.*s\n",
            operation, error_length, error_text);
    MPI_Abort(MPI_COMM_WORLD, error_code);
}

static void make_directory(const char *path)
{
    if (mkdir(path, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "mkdir(%s) failed: %s\n",
                path, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv)
{
    int error = MPI_Init(&argc, &argv);
    if (error != MPI_SUCCESS) {
        mpi_fail(error, "MPI_Init");
    }

    int rank = -1;
    int world_size = 0;

    if ((error = MPI_Comm_rank(MPI_COMM_WORLD, &rank)) != MPI_SUCCESS) {
        mpi_fail(error, "MPI_Comm_rank");
    }
    if ((error = MPI_Comm_size(MPI_COMM_WORLD, &world_size)) != MPI_SUCCESS) {
        mpi_fail(error, "MPI_Comm_size");
    }

    char hostname[MPI_MAX_PROCESSOR_NAME];
    int hostname_length = 0;

    if ((error = MPI_Get_processor_name(hostname, &hostname_length))
        != MPI_SUCCESS) {
        mpi_fail(error, "MPI_Get_processor_name");
    }
    hostname[hostname_length] = '\0';

    long maximum_iterations = 0;
    const char *log_directory = ".";

    if (argc >= 2) {
        char *end = NULL;
        errno = 0;
        maximum_iterations = strtol(argv[1], &end, 10);

        if (errno != 0 || end == argv[1] || *end != '\0'
            || maximum_iterations < 0) {
            fprintf(stderr, "Invalid iteration limit: %s\n", argv[1]);
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }
    }

    if (argc >= 3) {
        log_directory = argv[2];
    }

    make_directory(log_directory);

    char log_path[PATH_CAPACITY];
    int length = snprintf(log_path, sizeof(log_path),
                          "%s/rank_%d_%s.log",
                          log_directory, rank, hostname);

    if (length < 0 || (size_t)length >= sizeof(log_path)) {
        fprintf(stderr, "Log path is too long.\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    FILE *log = fopen(log_path, "a");
    if (log == NULL) {
        fprintf(stderr, "fopen(%s) failed: %s\n",
                log_path, strerror(errno));
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(log, NULL, _IOLBF, 0);

    for (long iteration = 0;
         maximum_iterations == 0 || iteration < maximum_iterations;
         ++iteration) {

        long local_value = 2L * iteration + rank;
        long global_sum = 0;

        error = MPI_Allreduce(&local_value,
                              &global_sum,
                              1,
                              MPI_LONG,
                              MPI_SUM,
                              MPI_COMM_WORLD);
        if (error != MPI_SUCCESS) {
            mpi_fail(error, "MPI_Allreduce");
        }

        char message[512];
        length = snprintf(
            message,
            sizeof(message),
            "rank=%d/%d host=%s pid=%ld iteration=%ld allreduce_sum=%ld\n",
            rank,
            world_size,
            hostname,
            (long)getpid(),
            iteration,
            global_sum
        );

        if (length < 0 || (size_t)length >= sizeof(message)) {
            fprintf(stderr, "Output message is too long.\n");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }

        fputs(message, stdout);
        fputs(message, log);
        sleep(1);
    }

    fclose(log);

    error = MPI_Finalize();
    if (error != MPI_SUCCESS) {
        mpi_fail(error, "MPI_Finalize");
    }

    return EXIT_SUCCESS;
}
