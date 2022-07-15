#ifndef CARTESIAN_H
#define CARTESIAN_H

#define MAX_PROCESSES 100
#define MAX_CART_PROP_SIZE 10

// This struct is used to store mapping of <comm_old_rank>, <comm_cart_rank> and
// <coordinates>.
typedef struct CartesianInfo {
  int comm_old_rank, comm_cart_rank;
  int coordinates[MAX_CART_PROP_SIZE];
} CartesianInfo;

// This struct is used to store the cartesian communicator properties, i.e., the
// arguments passed to the MPI_Cart_Create() API.
// For reference,
// int MPI_Cart_create(MPI_Comm comm_old, int ndims, const int dims[],
//                    const int periods[], int reorder, MPI_Comm * comm_cart)
typedef struct CartesianProperties {
  int comm_old_size;
  int comm_cart_size;
  int comm_old_rank;
  int comm_cart_rank;
  int coordinates[MAX_CART_PROP_SIZE];
  int ndims;
  int dimensions[MAX_CART_PROP_SIZE];
  int periods[MAX_CART_PROP_SIZE];
  int reorder;
} CartesianProperties;

#endif // ifndef CARTESIAN_H
