#include <gtest/gtest.h>

#include <mpi.h>

#include "record-replay.h"
#include "virtual-ids.h"

#undef DMTCP_PLUGIN_ENABLE_CKPT
#undef DMTCP_PLUGIN_DISABLE_CKPT
#undef JUMP_TO_LOWER_HALF
#undef RETURN_TO_UPPER_HALF


class CommTests : public ::testing::Test
{
  protected:
    MPI_Comm _comm;
    MPI_Comm _virtComm;

    void SetUp() override
    {
      int flag = 0;
      this->_comm = MPI_COMM_WORLD;
      this->_virtComm = -1;
      if (MPI_Initialized(&flag) == MPI_SUCCESS && !flag) {
        MPI_Init(NULL, NULL);
      }
    }

    void TearDown() override
    {
      CLEAR_LOG();
      // MPI_Finalize();
    }
};

TEST_F(CommTests, testCommDup)
{
  EXPECT_EQ(VIRTUAL_TO_REAL_COMM(_comm), MPI_COMM_WORLD);
  MPI_Comm real1 = MPI_COMM_NULL;
  // Create Comm dup
  EXPECT_EQ(MPI_Comm_dup(_comm, &real1), MPI_SUCCESS);
  EXPECT_NE(real1, MPI_COMM_NULL);
  // Add it to virtual table
  _virtComm = ADD_NEW_COMM(real1);
  EXPECT_NE(_virtComm, -1);
  MPI_Comm oldvirt = _virtComm;
  EXPECT_EQ(VIRTUAL_TO_REAL_COMM(_comm), MPI_COMM_WORLD);
  // Log the call
  LOG_CALL(restoreComms, Comm_dup, _comm, _virtComm);
  // Replay the call
  EXPECT_EQ(RESTORE_MPI_STATE(), MPI_SUCCESS);
  // Verify state after replay
  EXPECT_EQ(_virtComm, oldvirt);
  EXPECT_NE(VIRTUAL_TO_REAL_COMM(_virtComm), real1);
}

TEST_F(CommTests, testCommSplit)
{
  EXPECT_EQ(VIRTUAL_TO_REAL_COMM(_comm), MPI_COMM_WORLD);
  MPI_Comm real1 = MPI_COMM_NULL;
  // Create Comm split
  int color = 0;
  int key = 0;
  EXPECT_EQ(MPI_Comm_split(_comm, color, key, &real1), MPI_SUCCESS);
  EXPECT_NE(real1, MPI_COMM_NULL);
  // Add it to virtual table
  _virtComm = ADD_NEW_COMM(real1);
  EXPECT_NE(_virtComm, -1);
  MPI_Comm oldvirt = _virtComm;
  LOG_CALL(restoreComms, Comm_split, _comm, color, key, _virtComm);
  // Replay the call
  EXPECT_EQ(RESTORE_MPI_STATE(), MPI_SUCCESS);
  // Verify state after replay
  EXPECT_EQ(_virtComm, oldvirt);
  EXPECT_NE(VIRTUAL_TO_REAL_COMM(_virtComm), real1);
}

TEST_F(CommTests, testCommCreate)
{
  EXPECT_EQ(VIRTUAL_TO_REAL_COMM(_comm), MPI_COMM_WORLD);
  MPI_Comm real1 = MPI_COMM_NULL;
  // Create Comm split
  MPI_Group group = -1;
  EXPECT_EQ(MPI_Comm_group(_comm, &group), MPI_SUCCESS);
  EXPECT_NE(group, -1);
  EXPECT_EQ(MPI_Comm_create(_comm, group, &real1), MPI_SUCCESS);
  EXPECT_NE(real1, MPI_COMM_NULL);
  // Add it to virtual table
  _virtComm = ADD_NEW_COMM(real1);
  EXPECT_NE(_virtComm, -1);
  MPI_Comm oldvirt = _virtComm;
  LOG_CALL(restoreComms, Comm_create, _comm, group, _virtComm);
  // Replay the call
  EXPECT_EQ(RESTORE_MPI_STATE(), MPI_SUCCESS);
  // Verify state after replay
  EXPECT_EQ(_virtComm, oldvirt);
  EXPECT_NE(VIRTUAL_TO_REAL_COMM(_virtComm), real1);
}

int
main(int argc, char **argv)
{
  initializeJalib();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
