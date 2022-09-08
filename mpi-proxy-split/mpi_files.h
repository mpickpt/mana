#ifndef MANA_FILES_H
#define MANA_FILES_H

#include <mpi.h>
#include <linux/limits.h>

typedef struct OpenFileParameters {
  // Initial file creation parameters
  MPI_Comm _comm;
  int _mode;
  MPI_Info _info;
  char _filepath[PATH_MAX];

  // File characteristics set after creation
  int _atomicity;
  MPI_Offset _size;
  MPI_Offset _viewDisp;
  MPI_Datatype _viewEType;
  MPI_Datatype _viewFType;
  MPI_Info _viewInfo;
  int _viewSet;
  char _datarep[16]; // One of internal, native, or external32

  // File characteristics not necessarily explicitly set, but must be restored
  MPI_Offset _offset;

} OpenFileParameters;

#endif // MANA_FILES
