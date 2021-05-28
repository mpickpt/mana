// Compile with -lrt, for shm_open()
// For shm_open()
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>

#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"

#include "mpi_plugin.h"
#include "drain_send_recv_packets.h"
#include "record-replay.h"
#include "mpi_nextfunc.h"
#include "two-phase-algo.h"
#include "virtual-ids.h"

using namespace dmtcp_mpi;

// These MPI functions are not really wrappers.  We write them fresh,
// and use other MPI calls and syscalls for their implementation.

/* MPI 3.1 standard:
 * 11.2.6 Window Attributes
 * The following attributes are cached with a window when the window is created.
 * MPI_WIN_BASE window base address.
 * MPI_WIN_SIZE window size, in bytes.
 * MPI_WIN_DISP_UNIT displacement unit associated with the window.
 * MPI_WIN_CREATE_FLAVOR how the window was created.
 * MPI_WIN_MODEL memory model for window.
 *
 * FIXME:  We should use:
 * 11.2.6: "will return in base a pointer to ...":  confusing; elsewhere
 *           baseptr is a pointer to the local offset in the window of
 *           the current rank.  "the start of the window win" apparently
 *           is the same as the baseptr with offset.  This should
 *           be clarified.  But then, are the attributes of a window object
 *           global (indpendent of the current rank) or local (current rank)?
 *           If they are global, then MPI_WIN_BASE is not the same as
 *           baseptr.  But then, what about non-contiguous shared memory?
 *  Another way of saying local or global is:
 *           Is MPI_Win_get/set_attr a collective call or not?
 *           If it's not a collective call, how do the attributes propagate
 *           on MPI_Win_set_attr?  If it is a collective call, then how does
 *           a single rank call it for local information?
 *
 *  Section 6.7.3: the Window caching functions) and Section 11.2 (the
 *       window attributes that are cached) should cross-reference each other.
 *
 * Section 11.2.6:
 * // The standard seems to say that we can set these attributes,
 * //   but MPICH returns "Cannot set permanent attribute" in:
 * MPI_Win_set_attr(*win, MPI_WIN_BASE (etc.), &baseOrigin);
 * The standard should explain this.
 * (But I prefer to let users do what they want, at their own risk.
 *  It makes it easier to place checkpoint-restart on top of MPI.)
 */



// Translate rank in comm to rank in MPI_COMM_WORLD
int rankInCommWorld(MPI_Comm comm, int rank) {
  MPI_Group group, groupWorld;
  MPI_Comm_group(comm, &group);
  MPI_Comm_group(MPI_COMM_WORLD, &groupWorld);
  int rank_out;
  MPI_Group_translate_ranks(group, 1, &rank, groupWorld, &rank_out);

  if (rank_out == MPI_PROC_NULL || rank_out == MPI_UNDEFINED) { return -1; }
  return rank_out;
}

static int virtWinId = 0;
// FIXME:  This is a local virtWinId.  We need to record here:
//          a globally unique id (hashing), the virtual id of the
//          communicator it comes from, and the info and an array
//          of baseptr's for the win.
static int virtWinIdToReal[2][10000];

// FIXME:  MPI says that size can be different in each rank, and
//         even disp_unit can be different from size.  We don't support that.
// FIXME:  MPI_Win_lock/unlock/lockall/put/get/fence not implemented for this.
// The info key alloc_shared_noncontig is assumed false.
// The output param, win, has virtWinId stored in first few bytes,
//   followed by baseptr.
// The window must be of "flavor" MPI_Win_flavor_shared, or else
//     return MPI_ERR_RMA_FLAVOR.  This flavor is the value for the
//     attribute MPI_WIN_CREATE_FLAVOR in MPI_Win_get/set_attr.
USER_DEFINED_WRAPPER(int, Win_allocate_shared,
                     (MPI_Aint) size, (int) disp_unit, (MPI_Info) info,
                     (MPI_Comm) comm, (void *) baseptr, (MPI_Win *) win)
{
  int rank = -1;
  int commSize = -1;
  int fd;
  char name[40];
  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size (comm, &commSize);
  if (disp_unit != size) {  // Make sure user is requesting contiguous.
    fprintf(stderr,
            "MPI_Win_allocate_shared called with size(%ld) != disp_unit(%d).\n"
            "Not supported in MANA.\n", size, disp_unit);
    MPI_Abort(comm, 1);
  }
  if (rank == 0) {
    int rankZeroWorld = rankInCommWorld(comm, 0);
    snprintf(name, sizeof(name),  "/WIN_ALLOCATE_SHARED-%d-%d",
             rankZeroWorld, virtWinId);
    JASSERT(strlen(name) < sizeof(name)) (name)(sizeof(name));
    virtWinId++;
    // Create /WIN_ALLOCATE_SHARED-rank-id
    fd = shm_open(name, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
    if (fd == -1) {
      perror("MPI_Win_allocate_shared: shm_open(O_CREAT)"); fflush(stderr);
      return errno;
    }
  }
  MPI_Bcast(name, sizeof(name), MPI_CHAR, 0, comm);
  if (rank != 0) {
    // Open /WIN_ALLOCATE_SHARED-rank-id created by rank 0
    fd = shm_open(name, O_RDWR, S_IRUSR|S_IWUSR);
    if (fd == -1) {
      perror("MPI_Win_allocate_shared: shm_open(no O_CREAT)"); fflush(stderr);
      return errno;
    }
  }
  int rc = ftruncate(fd, commSize*disp_unit);
  if (rc == -1) {
    perror("MPI_Win_allocate_shared: ftruncate"); fflush(stderr);
    return errno;
  }
  void * baseOrigin = mmap(NULL, commSize*disp_unit,
                           PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (baseOrigin == MAP_FAILED) {
    perror("MPI_Win_allocate_shared: mmap"); fflush(stderr);
    return errno;
  }
  *(void **)baseptr = baseOrigin + rank * disp_unit;
  // We need to set win to something.  So we'll return a trivial shared window.
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  rc = NEXT_FUNC(Win_allocate_shared)(0, disp_unit, info, realComm, NULL, win);
  if (rc != MPI_SUCCESS) {
    perror("MPI_Win_allocate_shared: lower half MPI library"); fflush(stderr);
    return errno;
  }
  // FIXME: We should add keyval MPI_MANA_WIN_BASE and MPI_MANA_WIN_SIZE
  //        to easily track attr_val, since MPI won't let us do this in advance.
#if 0
  // The MPI-3 standard seems to say that we can set these attributes,
  //   but MPICH returns: "Cannot set permanent attribute"
  MPI_Win_set_attr(*win, MPI_WIN_BASE, &baseOrigin);
  MPI_Win_set_attr(*win, MPI_WIN_SIZE, &size);
  MPI_Win_set_attr(*win, MPI_WIN_DISP_UNIT, &disp_unit);
  MPI_Win_set_attr(*win, MPI_WIN_CREATE_FLAVOR, &MPI_WIN_FLAVOR_SHARED);
  MPI_Win_set_attr(*win, MPI_WIN_MODEL, &MPI_WIN_UNIFIED);
#endif
  // FIXME:  Add LOG_AND_REPLAY()
  // FIXME:  During REPLAY, we need to verify that this communiicator is
  //         a single shared memory domain.  We can test this with
  //         MPI_Comm_split_type() on the current communicator
  //         and verify that the new shared memory communicator is the
  //         same size.
  return MPI_SUCCESS;
}

// This could be point-to-point.  But we cache it.  It's not a collective call.
// FIXME:  We should cache the size, disp_unit, baseptr.  The remote
//         rank might be blocked in a collective communication.
//         We could cache it locally, or else inside the shared memory domain.
//         Maybe we should create a real window, with attributes for these,
//         and then set the attributes at the time that we create the window.
//         Then MPI_Win_shared_query can be passed down to the MPI library.
//  Section 6.7.3 has the Window caching functions
//  Section 11.2 has the actual window attributes that are cached.
USER_DEFINED_WRAPPER(int, Win_shared_query, (MPI_Win) win, (int) rank,
                     (MPI_Aint *) size, (int *) disp_unit, (void *) baseptr)
{
  JASSERT(false)("wrapper: `MPI_Win_shared_query` not implemented");
  return -1; // To satisfy the compiler
}


PMPI_IMPL(int, MPI_Win_allocate_shared,
          MPI_Aint size, int disp_unit, MPI_Info info,
          MPI_Comm comm, void * baseptr, MPI_Win * win)
PMPI_IMPL(int, MPI_Win_shared_query, MPI_Win win, int rank,
          MPI_Aint * size, int * disp_unit, void * baseptr)
