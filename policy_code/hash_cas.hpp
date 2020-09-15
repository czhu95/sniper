#include "stdio.h"
#include "include/sim_api.h"
#include "stdint.h"
#include <cassert>

#include "policy.hpp"
#include "msg.h"

#define MAX_REQ 16
#define INVALID_ADDR ((uint64_t)-1)

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

class HashCAS : public Policy
{
   uint64_t start, end, length;
   const uint64_t block_logsize = 20;
   const uint64_t block_size = 1UL << block_logsize;
   const uint64_t block_mask = ~(block_size - 1);
   const uint64_t num_nodes = 4;
   const uint64_t app_cores = 32;
   const uint64_t shared_cores = app_cores / num_nodes;

   bool *block_map; // [length / block_size + 1];
   uint64_t node_id = -1;
   uint64_t checksum;

   int head = 0, tail = 0;

public:
   HashCAS()
   {
   }

   void Exec(GMMCoreMessage &msg) override
   {
      switch (msg.type)
      {
         case POLICY_INIT:
            node_id = msg.requester;
            start = msg.payload[0];
            end = msg.payload[1];
            length = end - start;
            block_map = new bool[length / block_size + 1];
            for (int i = 0; i < length / block_size + 1; i ++)
               block_map[i] = false;

            SimGMMCoreMovType(GMM_CORE_DONE);
            SimGMMCoreMessage();
            break;
         case SH_REQ:
         {
            // msg.payload[0] = msg.payload[0] & ~((1UL << 20) - 1);
            // msg.payload[1] = msg.payload[1] + 1UL << 20;
            // printf("here\n");
            uint64_t addr = msg.payload[0];
            // printf("addr: %lx\n", addr);
            int block_num = (addr >> block_logsize) - (start >> block_logsize);
            // printf("blocknum: %d\n", block_num);
            if (!block_map[block_num])
            {
               block_map[block_num] = true;

               SimGMMCoreMovType(TLB_INSERT);
               SimGMMCoreMovComponent(GMM_CORE);
               SimGMMCoreMovRecv(node_id);

               uint64_t seg_start = MAX(addr & block_mask, start);
               uint64_t seg_end = MIN((addr & block_mask) + block_size, end);
               SimGMMCoreMovPayload(seg_start, seg_end);

               SimGMMCoreMovPayload1(seg_start);
               SimGMMCoreMovPayload2(seg_end);

               // printf("[GMM Core] Send GMM message type = %d, start = %lx, end = %lx\n",
               //        msg.type, msg.payload[0], msg.payload[1]);
               SimGMMCoreMessage();
            }
            break;
         }
         case ATOMIC_UPDATE_REQ:
         {
            SimGMMCoreMovRecv(node_id);
            SimGMMCoreMovType(USER_CACHE_READ_REQ);
            SimGMMCoreMovComponent(LAST_LEVEL_CACHE);
            SimGMMCoreMessage();
            break;
         }
         case ATOMIC_UPDATE_MSG:
         {
            if (msg.requester / shared_cores == node_id - app_cores)
            {
               SimGMMCoreMovRecv(msg.requester);
               SimGMMCoreMovRecv(msg.requester);
               SimGMMCoreMovType(GMM_USER_DONE);
               SimGMMCoreMovComponent(CORE);
               SimGMMCoreMessage();
            }
            break;
         }
         case USER_CACHE_READ_REP:
         {
            SimGMMCoreMovType(ATOMIC_UPDATE_MSG);
            SimGMMCoreMovComponent(GMM_CORE);
            SimGMMCoreMessage();
            if (msg.requester / shared_cores == node_id - app_cores)
            {
               SimGMMCoreMovRecv(msg.requester);
               SimGMMCoreMovRecv(msg.requester);
               SimGMMCoreMovType(GMM_USER_DONE);
               SimGMMCoreMovComponent(CORE);
               SimGMMCoreMessage();
            }
            break;
         }
         default:
            SimGMMCoreMovType(GMM_CORE_DONE);
            SimGMMCoreMessage();

            break;
      }

      // printf("[GMM Core] Pull GMM message policy = %d, type = %d, sender = %d, addr = %lx\n",
      //        msg.policy, msg.type, msg.sender, msg.payload[0]);


   }


};

// const uint64_t start = 0x55555ee516f0;
// const uint64_t length = 40000000;
// const uint64_t start = 0x55555577cca0;
// const uint64_t length = 2832;

