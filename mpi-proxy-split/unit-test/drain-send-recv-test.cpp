#include <gtest/gtest.h>

#include <mpi.h>
#include <string.h>

#include "dmtcp.h"
#include "p2p_drain_send_recv.h"
#include "libproxy.h"
#include "lookup_service.h"
#include "lower_half_api.h"
#include "split_process.h"
#include "virtual-ids.h"

#undef DMTCP_PLUGIN_ENABLE_CKPT
#undef DMTCP_PLUGIN_DISABLE_CKPT
#undef JUMP_TO_LOWER_HALF
#undef RETURN_TO_UPPER_HALF
#undef NEXT_FUNC

using namespace dmtcp_mpi;

static dmtcp::LookupService lsObj;
proxyDlsym_t pdlsym = NULL;
LowerHalfInfo_t lh_info;
int g_numMmaps = 0;
MmapInfo_t *g_list = NULL;
MemRange_t *g_range = NULL;

// Mock functions
int
MPI_Comm_create_group_internal(MPI_Comm comm, MPI_Group group,
                                   int tag, MPI_Comm *newcomm)
{
  int retval;
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Group realGroup = VIRTUAL_TO_REAL_GROUP(group);
  retval = MPI_Comm_create_group(realComm, realGroup, tag, newcomm);
  return retval;
}

int
MPI_Comm_free_internal(MPI_Comm *comm)
{
  int retval;
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(*comm);
  retval = MPI_Comm_free(&realComm);
  return retval;
}

int
MPI_Alltoall_internal(const void *sendbuf, int sendcount,
                      MPI_Datatype sendtype, void *recvbuf, int recvcount,
                      MPI_Datatype recvtype, MPI_Comm comm)
{
  int retval;
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Datatype realSendType = VIRTUAL_TO_REAL_TYPE(sendtype);
  MPI_Datatype realRecvType = VIRTUAL_TO_REAL_TYPE(recvtype);
  retval = MPI_Alltoall(sendbuf, sendcount, realSendType, recvbuf,
                        recvcount, realRecvType, realComm);
  return retval;
}

int
MPI_Test_internal(MPI_Request *request, int *flag, MPI_Status *status,
                  bool isRealRequest)
{
  int retval;
  MPI_Request realRequest;
  if (isRealRequest) {
    realRequest = *request;
  } else {
    realRequest = VIRTUAL_TO_REAL_REQUEST(*request);
  }
  // MPI_Test can change the *request argument
  retval = MPI_Test(&realRequest, flag, status);
  return retval;
}

SwitchContext::SwitchContext(unsigned long lowerHalfFs)
{
}

SwitchContext::~SwitchContext()
{
}

class DrainTests : public ::testing::Test
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
      initialize_drain_send_recv();
      resetDrainCounters();
    }

    void TearDown() override
    {
      // MPI_Finalize();
    }
};

TEST_F(DrainTests, testSendDrain)
{
  const int TWO = 2;
  int sbuf = 5;
  int rbuf = 0;
  int flag = 0;
  MPI_Request reqs[TWO] = {0};
  MPI_Status sts[TWO] = {0};
  int size = 0;
  EXPECT_EQ(MPI_Type_size(MPI_INT, &size), MPI_SUCCESS);
  for (int i = 0; i < TWO; i++) {
    EXPECT_EQ(MPI_Isend(&sbuf, 1, MPI_INT, 0, 0, _comm, &reqs[i]),
              MPI_SUCCESS);
    addPendingRequestToLog(ISEND_REQUEST, &sbuf, NULL, 1,
                           MPI_INT, 0, 0, _comm, reqs[i]);
  }
  getLocalRankInfo();
  registerLocalSendsAndRecvs();
  drainSendRecv();
  for (int i = 0; i < TWO; i++) {
    EXPECT_EQ(consumeBufferedPacket(&rbuf, 1, MPI_INT, 0,
                                    0, _comm, &sts[i], size), MPI_SUCCESS);
    EXPECT_EQ(rbuf, sbuf);
  }
}

TEST_F(DrainTests, testSendDrainOnDiffComm)
{
  const int TWO = 2;
  int sbuf = 5;
  int rbuf = 0;
  int flag = 0;
  MPI_Request reqs[TWO] = {0};
  MPI_Status sts[TWO] = {0};
  int size = 0;
  MPI_Comm newcomm = MPI_COMM_NULL;
  EXPECT_EQ(MPI_Comm_dup(_comm, &newcomm), MPI_SUCCESS);
  EXPECT_EQ(MPI_Type_size(MPI_INT, &size), MPI_SUCCESS);
  for (int i = 0; i < TWO; i++) {
    EXPECT_EQ(MPI_Isend(&sbuf, 1, MPI_INT, 0, 0, newcomm, &reqs[i]),
              MPI_SUCCESS);
    addPendingRequestToLog(ISEND_REQUEST, &sbuf, NULL, 1,
                           MPI_INT, 0, 0, newcomm, reqs[i]);
  }
  getLocalRankInfo();
  registerLocalSendsAndRecvs();
  drainSendRecv();
  for (int i = 0; i < TWO; i++) {
    int rc;
    EXPECT_TRUE(isBufferedPacket(0, 0, newcomm, &flag, &sts[i]));
    EXPECT_EQ(consumeBufferedPacket(&rbuf, 1, MPI_INT, 0,
                                    0, newcomm, &sts[i], size), MPI_SUCCESS);
    EXPECT_EQ(rbuf, sbuf);
  }
}

TEST_F(DrainTests, testRecvDrain)
{
  const int TWO = 2;
  int sbuf = 5;
  int rbuf = 0;
  int flag = 0;
  MPI_Request reqs[TWO] = {0};
  MPI_Status sts[TWO] = {0};
  int size = 0;
  MPI_Comm newcomm = MPI_COMM_NULL;
  EXPECT_EQ(MPI_Comm_dup(_comm, &newcomm), MPI_SUCCESS);
  EXPECT_EQ(MPI_Type_size(MPI_INT, &size), MPI_SUCCESS);
  for (int i = 0; i < TWO; i++) {
    int source = 0;
    int tag = 0;
    if (i == 0) {
      source = MPI_ANY_SOURCE;
      tag = MPI_ANY_TAG;
    }
    EXPECT_EQ(MPI_Irecv(&rbuf, 1, MPI_INT, source, tag, newcomm, &reqs[i]),
              MPI_SUCCESS);
    addPendingRequestToLog(IRECV_REQUEST, NULL, &rbuf, 1,
                           MPI_INT, 0, 0, newcomm, reqs[i]);
  }
  // Checkpoint
  getLocalRankInfo();
  registerLocalSendsAndRecvs();
  drainSendRecv();
  // Resume
  for (int i = 0; i < TWO; i++) {
    int rc;
    EXPECT_EQ(MPI_Send(&sbuf, 1, MPI_INT, 0, 0, newcomm), MPI_SUCCESS);
    MPI_Wait(&reqs[i], &sts[i]);
    EXPECT_EQ(rbuf, sbuf);
  }
  // Restart
  replayMpiP2pOnRestart();
  // Resume
  for (int i = 0; i < TWO; i++) {
    int rc;
    EXPECT_EQ(MPI_Send(&sbuf, 1, MPI_INT, 0, 0, newcomm), MPI_SUCCESS);
    MPI_Wait(&reqs[i], &sts[i]);
    EXPECT_EQ(rbuf, sbuf);
  }
}

int
main(int argc, char **argv, char **envp)
{
  initializeJalib();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
