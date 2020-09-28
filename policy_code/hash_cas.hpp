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
   const uint64_t num_nodes = 8;
   const uint64_t app_cores = 32;
   const uint64_t shared_cores = app_cores / num_nodes;
   const float mem_cap = 1.;
   // const int cache_nodes1[4] = {19, 18, 17, 16};
   // const int cache_nodes1[4] = {35, 34, 33, 32};
   const int cache_nodes1[8] = {39, 38, 37, 36, 35, 34, 33, 32};
   // const int cache_nodes1[8] = {71, 70, 69, 68, 67, 66, 65, 64};
   // const int cache_nodes1[4] = {18, 19, 16, 17};
   // const int cache_nodes2[4] = {17, 16, 19, 18};

   bool *block_map; // [length / block_size + 1];
   uint64_t node_id = -1;
   uint64_t checksum;
   uint64_t cache_blocks = 0;
   uint64_t max_cache_blocks = 0;

   int get_home(uint64_t block_num)
   {
      return (block_num & (num_nodes - 1)) + app_cores;
   }


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
         {
            node_id = msg.requester;
            start = msg.payload[0];
            end = msg.payload[1];
            length = end - start;
            int num_blocks = length / block_size + 1;
            max_cache_blocks = int(num_blocks * (mem_cap > .25 ? mem_cap - .25 : mem_cap));
            // max_cache_blocks = int(num_blocks * mem_cap);
            block_map = new bool[num_blocks];
            for (int i = 0; i < length / block_size + 1; i ++)
               block_map[i] = false;

            SimGMMCoreMovType(GMM_CORE_DONE);
            SimGMMCoreMessage();
            break;
         }
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
               int node = get_home(block_num);
               // printf("node id: %d", node);
               if (mem_cap <= .25)
               {
                  if (cache_blocks < max_cache_blocks && cache_nodes1[node_id - app_cores] == node)
                  {
                     cache_blocks ++;
                     node = node_id;
                  }
               }
               else
               {
                  if (cache_nodes1[node_id - app_cores] == node)
                  {
                     node = node_id;
                  }
                  else if (cache_blocks < max_cache_blocks && node != node_id)
                  {
                     cache_blocks ++;
                     node = node_id;
                  }
               }
               // if (cache_blocks < max_cache_blocks && node != node_id)
               // {
               //    cache_blocks ++;
               //    node = node_id;
               // }
               block_map[block_num] = true;
               SimGMMCoreMovType(TLB_INSERT);
               SimGMMCoreMovComponent(GMM_CORE);
               SimGMMCoreMovRecv(node_id);

               uint64_t seg_start = MAX(addr & block_mask, start);
               uint64_t seg_end = MIN((addr & block_mask) + block_size, end);
               seg_end |= ((uint64_t)node << 48);
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
