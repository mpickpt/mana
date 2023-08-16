#include <gtest/gtest.h>

#include <mpi.h>

#include "record-replay.h"
#include "virtual-ids.h"

#undef DMTCP_PLUGIN_ENABLE_CKPT
#undef DMTCP_PLUGIN_DISABLE_CKPT
#undef JUMP_TO_LOWER_HALF
#undef RETURN_TO_UPPER_HALF


class GroupTest : public ::testing::Test
{
  protected:
    MPI_Comm _comm;

    void SetUp() override
    {
      int flag = 0;
      this->_comm = MPI_COMM_WORLD;
      if (MPI_Initialized(&flag) == MPI_SUCCESS && !flag) {
        MPI_Init(NULL, NULL);
      }
    }

    void TearDown() override
    {
      // MPI_Finalize();
      CLEAR_LOG();
    }
};

TEST_F(GroupTest, testGroupAPI)
{
  MPI_Group group = MPI_GROUP_NULL;
  EXPECT_EQ(MPI_Comm_group(_comm, &group), MPI_SUCCESS);
  EXPECT_NE(group, MPI_GROUP_NULL);
  int size = -1;
  EXPECT_EQ(MPI_Group_size(group, &size), MPI_SUCCESS);
  EXPECT_EQ(size, 1);
}

int
main(int argc, char **argv)
{
  initializeJalib();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
