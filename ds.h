#define MAX_PROCESSES 100
#define MAX_CART_PROP_SIZE 10

typedef struct CartesianInfo {
  int comm_old_rank, comm_cart_rank;
  int coordinates[MAX_CART_PROP_SIZE];
} CartesianInfo;

typedef struct CartesianProperties {
  int old_comm_size;
  int new_comm_size;
  int old_rank, new_rank;
  int coordinates[MAX_CART_PROP_SIZE];
  int number_of_dimensions;
  int dimensions[MAX_CART_PROP_SIZE];
  int periods[MAX_CART_PROP_SIZE];
  int reorder;
} CartesianProperties;

