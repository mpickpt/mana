/****************************************************************************
 *   Copyright (C) 2019-2021 by Gene Cooperman, Rohan Garg, Yao Xu          *
 *   gene@ccs.neu.edu, rohgarg@ccs.neu.edu, xu.yao1@northeastern.edu        *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License in the files COPYING and COPYING.LESSER.  If not, see           *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#ifndef _MANA_COORDINATOR_
#define _MANA_COORDINATOR_

#include "dmtcp_coordinator.h"
#include "dmtcpmessagetypes.h"
#include "lookup_service.h"

void printNonReadyRanks();
dmtcp::string getClientState(dmtcp::CoordClient* client);
void printMpiDrainStatus(const dmtcp::LookupService& lookupService);
void processPreSuspendClientMsgHelper(dmtcp::DmtcpCoordinator *coord,
                                      dmtcp::CoordClient *client,
                                      int &workersAtCurrentBarrier,
                                      const dmtcp::DmtcpMessage& msg,
                                      const void *extraData);
void sendCkptIntentMsg(dmtcp::DmtcpCoordinator *coord);

#endif // ifndef _MANA_COORDINATOR_
