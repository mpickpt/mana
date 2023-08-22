#include "workerstate.h"
#include "dmtcp_restart.h"
#include "jassert.h"
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

  vector<char *> mtcpArgs = getMtcpArgs();
  mtcpArgs.push_back((char *)"--mpi");

  const map<string, string> &kvmap = t->getKeyValueMap();

  mtcpArgs.push_back((char*) "--minLibsStart");
  mtcpArgs.push_back((char*) kvmap.at("MANA_MinLibsStart").c_str());

  mtcpArgs.push_back((char*) "--maxLibsEnd");
  mtcpArgs.push_back((char*) kvmap.at("MANA_MaxLibsEnd").c_str());

  mtcpArgs.push_back((char*) "--minHighMemStart");
  mtcpArgs.push_back((char*) kvmap.at("MANA_MinHighMemStart").c_str());

  mtcpArgs.push_back((char*) "--maxHighMemEnd");
  mtcpArgs.push_back((char*) kvmap.at("MANA_MaxHighMemEnd").c_str());

  if (!restartDir.empty()) {
    mtcpArgs.push_back((char *)"--restartdir");
    mtcpArgs.push_back((char *)restartDir.c_str());
  }

  for (const string &image : ckptImages) {
    mtcpArgs.push_back((char*) image.c_str());
  }

  mtcpArgs.push_back(NULL);
  execvp(mtcpArgs[0], &mtcpArgs[0]);
  JASSERT(false)(mtcpArgs[0]).Text("execvp failed!");
}
