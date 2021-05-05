#pragma once

#include "Stats/StatsData.h"
#include "Stats/Stats2.h"

DECLARE_STATS_GROUP(TEXT("RenderStream"), STATGROUP_RenderStream, STATCAT_Advanced);

DECLARE_CYCLE_STAT(TEXT("Await Frame (Controller)"), STAT_AwaitFrame, STATGROUP_RenderStream);
DECLARE_CYCLE_STAT(TEXT("Receive Frame (Follower)"), STAT_ReceiveFrame, STATGROUP_RenderStream);
