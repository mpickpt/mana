#define MAX_PROCESSES 100
#define MAX_CART_PROP_SIZE 10

typedef struct CartesianInfo {
  int comm_old_rank, comm_cart_rank;
  int coordinates[MAX_CART_PROP_SIZE];
} CartesianInfo;

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

