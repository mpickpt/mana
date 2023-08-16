#include <gtest/gtest.h>

#include <mpi.h>

#include "record-replay.h"
#include "virtual-ids.h"

#undef DMTCP_PLUGIN_ENABLE_CKPT
#undef DMTCP_PLUGIN_DISABLE_CKPT
#undef JUMP_TO_LOWER_HALF
#undef RETURN_TO_UPPER_HALF


class TypesTests : public ::testing::Test
{
  protected:
    int _count;
    int *_array;
    const size_t len = 100;

    void SetUp() override
    {
      int flag = 0;
      this->_count = 10;
      this->_array = new int[len];
      if (MPI_Initialized(&flag) == MPI_SUCCESS && !flag) {
        MPI_Init(NULL, NULL);
      }
    }

    void TearDown() override
    {
      if (this->_array) {
        delete this->_array;
      }
      CLEAR_LOG();
      // MPI_Finalize();
    }

};

TEST_F(TypesTests, testTypeContiguous)
{
  MPI_Datatype type = MPI_INT;
  MPI_Datatype real1 = MPI_DATATYPE_NULL;

  EXPECT_EQ(VIRTUAL_TO_REAL_TYPE(type), MPI_INT);
  EXPECT_EQ(VIRTUAL_TO_REAL_TYPE(real1), MPI_DATATYPE_NULL);

  EXPECT_EQ(MPI_Type_contiguous(_count, type, &real1), MPI_SUCCESS);
  EXPECT_NE(real1, MPI_DATATYPE_NULL);
  MPI_Datatype virtType = ADD_NEW_TYPE(real1);
  EXPECT_EQ(VIRTUAL_TO_REAL_TYPE(virtType), real1);

  EXPECT_TRUE(LOG_CALL(restoreTypes, Type_contiguous,
                       _count, type, virtType) != NULL);
  EXPECT_EQ(RESTORE_MPI_STATE(), MPI_SUCCESS);

  EXPECT_NE(VIRTUAL_TO_REAL_TYPE(virtType), real1);
}

TEST_F(TypesTests, testTypeCommit)
{
  MPI_Datatype type = MPI_INT;
  MPI_Datatype real1 = MPI_DATATYPE_NULL;

  EXPECT_EQ(VIRTUAL_TO_REAL_TYPE(type), MPI_INT);
  EXPECT_EQ(VIRTUAL_TO_REAL_TYPE(real1), MPI_DATATYPE_NULL);

  // Ask MPI for a new datatype
  EXPECT_EQ(MPI_Type_contiguous(_count, type, &real1), MPI_SUCCESS);
  EXPECT_NE(real1, MPI_DATATYPE_NULL);
  MPI_Datatype virtType = ADD_NEW_TYPE(real1);
  EXPECT_EQ(VIRTUAL_TO_REAL_TYPE(virtType), real1);
  EXPECT_TRUE(LOG_CALL(restoreTypes, Type_contiguous,
                       _count, type, virtType) != NULL);

  // Commit the new datatype
  EXPECT_EQ(MPI_Type_commit(&real1), MPI_SUCCESS);
  EXPECT_TRUE(LOG_CALL(restoreTypes, Type_commit, real1) != NULL);
  EXPECT_EQ(VIRTUAL_TO_REAL_TYPE(virtType), real1);
  int size = -1;
  EXPECT_EQ(MPI_Type_size(real1, &size), MPI_SUCCESS);
  EXPECT_EQ(size, _count * sizeof(*_array));

  // Replay the log
  EXPECT_EQ(RESTORE_MPI_STATE(), MPI_SUCCESS);

  // Verify after replaying the log
  MPI_Datatype real2 = VIRTUAL_TO_REAL_TYPE(virtType);
  EXPECT_NE(real2, real1);
  EXPECT_EQ(MPI_Type_size(real2, &size), MPI_SUCCESS);
  EXPECT_EQ(size, _count * sizeof(*_array));
}

int
main(int argc, char **argv)
{
  initializeJalib();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
