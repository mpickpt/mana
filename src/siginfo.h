#ifndef SIGINFO_H
#define SIGINFO_H

#include <signal.h>

namespace dmtcp
{
namespace SigInfo
{
int ckptSignal();
void setupCkptSigHandler(sighandler_t handler);
void saveSigHandlers();
void restoreSigHandlers();
}
}

#if 0
extern volatile bool inTrivialBarrierOrPhase1;
extern ucontext_t beforeTrivialBarrier;
#endif
#endif // ifndef SIGINFO_H
