#include "workerstate.h"
#include "dmtcp_restart.h"
#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "util.h"

using namespace dmtcp;

void dmtcp_restart_plugin(const string &restartDir,
                          const vector<string>& ckptImages)
{
  string image_zero;

  JASSERT(restartDir.empty() ^ ckptImages.empty());

  WorkerState::setCurrentState(WorkerState::RESTARTING);

  if (restartDir.empty()) {
    image_zero = ckptImages[0];
  } else {
    string image_zero_dir = restartDir + "/ckpt_rank_0/";
    vector<string> files = jalib::Filesystem::ListDirEntries(image_zero_dir);

    for (const string &file : files) {
      if (Util::strStartsWith(file.c_str(), "ckpt") &&
          Util::strEndsWith(file.c_str(), ".dmtcp")) {
        image_zero = image_zero_dir + file;
        break;
      }
    }
  }

  JASSERT(!image_zero.empty()).Text("Failed to locate first checkpoint file!");

  // read dmtcp files off underlying filesystem
  RestoreTarget *t = new RestoreTarget(image_zero);

  // Connect with coordinator using the first checkpoint image in the list
  // Also, create the DMTCP shared-memory area.
  t->initialize();

  publishKeyValueMapToMtcpEnvironment(t);

  if (!restartDir.empty()) {
    setenv("MANA_RestartDir=", restartDir.c_str(), 1);
  }

  for (size_t i = 0; i < ckptImages.size(); i++) {
    string key = "MANA_CkptImage_Rank_" + jalib::XToString(i);
    setenv(key.c_str(), ckptImages[i].c_str(), 1);
  }

  vector<char *> mtcpArgs = getMtcpArgs();
  mtcpArgs.push_back((char *)"--mpi");

  mtcpArgs.push_back(NULL);
  execvp(mtcpArgs[0], &mtcpArgs[0]);
  JASSERT(false)(mtcpArgs[0]).Text("execvp failed!");
}