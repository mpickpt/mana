#include <gtest/gtest.h>

#include <mpi.h>
#include <string.h>

#include "record-replay.h"
#include "virtual-ids.h"

#undef DMTCP_PLUGIN_ENABLE_CKPT
#undef DMTCP_PLUGIN_DISABLE_CKPT
#undef JUMP_TO_LOWER_HALF
#undef RETURN_TO_UPPER_HALF


class CartTests : public ::testing::Test
{
  protected:
    MPI_Comm _comm;
    int _ndims;
    int *_dims;
    int *_periods;
    int *_coords;
    int _reorder;

    void SetUp() override
    {
      int flag = 0;
      this->_comm = MPI_COMM_WORLD;
      this->_ndims = 1;
      this->_dims = new int[this->_ndims];
      this->_periods = new int[this->_ndims];
      this->_coords = new int[this->_ndims];
      this->_reorder = 0;
      if (MPI_Initialized(&flag) == MPI_SUCCESS && !flag) {
        MPI_Init(NULL, NULL);
      }
      this->_dims[0] = 1;
      this->_periods[0] = 0;
      this->_coords[0] = 0;
    }

    void TearDown() override
    {
      if (this->_dims) {
        delete this->_dims;
      }
      if (this->_periods) {
        delete this->_periods;
      }
      CLEAR_LOG();
      // MPI_Finalize();
    }
};

TEST_F(CartTests, testCartCreate)
{
  // Create a new cart comm
  MPI_Comm real1 = MPI_COMM_NULL;
  int newrank1 = -1;
  EXPECT_EQ(MPI_Cart_create(_comm, _ndims, _dims, _periods, _reorder, &real1),
            MPI_SUCCESS);
  EXPECT_NE(real1, MPI_COMM_NULL);
  MPI_Comm virtComm = ADD_NEW_COMM(real1);
  EXPECT_EQ(VIRTUAL_TO_REAL_COMM(virtComm), real1);
  // EXPECT_EQ(REAL_TO_VIRTUAL_COMM(real1), virtComm); TODO We comment this out for now since R->V is not well defined.
  EXPECT_EQ(MPI_Cart_map(real1, _ndims, _dims, _periods, &newrank1),
            MPI_SUCCESS);
  EXPECT_EQ(MPI_Cart_get(real1, _ndims, _dims, _periods, _coords),
            MPI_SUCCESS);
  // Log the call
  FncArg ds = CREATE_LOG_BUF(_dims, _ndims);
  FncArg ps = CREATE_LOG_BUF(_periods, _ndims);
  EXPECT_TRUE(LOG_CALL(restoreCarts, Cart_create, _comm, _ndims,
                       ds, ps, _reorder, virtComm) != NULL);
  // Replay the call
  EXPECT_EQ(RESTORE_MPI_STATE(), MPI_SUCCESS);
  MPI_Comm real2 = VIRTUAL_TO_REAL_COMM(virtComm);
  EXPECT_NE(real1, real2);

  // Test the state after replay
  int newrank2 = -1;
  int dims[_ndims], periods[_ndims], coords[_ndims];
  EXPECT_EQ(MPI_Cart_get(real2, _ndims, dims, periods, coords), MPI_SUCCESS);
  EXPECT_EQ(memcmp(dims, _dims, _ndims), 0);
  EXPECT_EQ(memcmp(periods, _periods, _ndims), 0);
  EXPECT_EQ(memcmp(coords, _coords, _ndims), 0);
  EXPECT_EQ(MPI_Cart_rank(real2, coords, &newrank2), MPI_SUCCESS);
  EXPECT_EQ(newrank1, newrank2);
}

TEST_F(CartTests, testCartMap)
{
  int newrank1 = -1;
  EXPECT_EQ(MPI_Cart_map(_comm, _ndims, _dims, _periods, &newrank1),
            MPI_SUCCESS);
  EXPECT_NE(newrank1, -1);
  FncArg ds = CREATE_LOG_BUF(_dims, _ndims * sizeof(int));
  FncArg ps = CREATE_LOG_BUF(_periods, _ndims * sizeof(int));
  EXPECT_TRUE(LOG_CALL(restoreCarts, Cart_map, _comm, _ndims,
                       ds, ps, newrank1) != NULL);
  EXPECT_EQ(RESTORE_MPI_STATE(), MPI_SUCCESS);
  // TODO: Not sure how to test that the mapping is still there
}

TEST_F(CartTests, testCartSub)
{
  // TODO:
}

int
main(int argc, char **argv)
{
  // FIXME: This unit test has been disabled because MPI cartesian communicator
  // is created at the restart step instead of the record-replay step, which
  // happens after the restart step. The new cartesian communicator created at
  // the restart step is then passed to record-replay separately via
  // setCartesianCommunicator(). Therefore, the "record-replay-cart" unit test
  // will result in seg fault error because <comm_cart_prime> variable (in
  // record-replay.cpp) has not been set in this unit test.
  //

  initializeJalib();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
