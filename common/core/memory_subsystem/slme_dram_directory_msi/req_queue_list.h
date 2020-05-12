#pragma once

#include <map>
#include <queue>

#include "shmem_req.h"
#include "req_queue_list_template.h"

namespace SingleLevelMemory
{
  typedef ReqQueueListTemplate<ShmemReq> ReqQueueList;
}
