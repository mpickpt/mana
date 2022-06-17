#define MAX_PROCESSES 100
#define MAX_CART_PROP_SIZE 10

typedef struct CartesianProperties {
  int old_comm_size;
  int new_comm_size;
  int old_rank, new_rank;
  int *coordinates;
  int number_of_dimensions;
  int *dimensions;
  int *periods;
  int reorder;
} CartesianProperties;

