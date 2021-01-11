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

#define BFS
// #define PageRank
// #define CC

class AtomicWriteAdd : public Policy
{
   uint64_t start, end, length;
   const uint64_t block_logsize = 12;
   const uint64_t block_size = 1UL << block_logsize;
   const uint64_t block_mask = ~(block_size - 1);
   const uint64_t num_nodes = 4;
   const uint64_t app_cores = 32;
   const float mem_cap = 1.;
   const int cache_nodes1[4] = {35, 34, 33, 32};

   bool *block_map; // [length / block_size + 1];
   uint64_t node_id = -1;
   uint64_t cache_blocks = 0;
   uint64_t max_cache_blocks = 0;
#ifdef BFS
   uint32_t *data;
#endif
#ifdef PageRank
   double *data;
#endif
#ifdef CC
   uint32_t *data;
#endif

   int get_home(uint64_t block_num)
   {
      return (block_num & (num_nodes - 1)) + app_cores;
   }

public:
   AtomicWriteAdd()
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
#ifdef BFS
            data = new uint32_t[length / sizeof(uint32_t)];
#endif
#ifdef PageRank
            data = new double[length / sizeof(double)];
#endif
#ifdef CC
            data = new uint32_t[length / sizeof(uint32_t)];
#endif
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
               // int node = get_home(block_num);
               int node = app_cores;
               // printf("node id: %d", node);
               // if (mem_cap <= .25)
               // {
               //    if (cache_blocks < max_cache_blocks && cache_nodes1[node_id - app_cores] == node)
               //    {
               //       cache_blocks ++;
               //       node = node_id;
               //    }
               // }
               // else
               // {
               //    if (cache_nodes1[node_id - app_cores] == node)
               //    {
               //       node = node_id;
               //    }
               //    else if (cache_blocks < max_cache_blocks && node != node_id)
               //    {
               //       cache_blocks ++;
               //       node = node_id;
               //    }
               // }

               // if (cache_blocks < max_cache_blocks /* && cache_nodes[node_id - app_cores] == node */)
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
            // printf("[GMM Core: %d] received atomic update req swap %lx <-> %lx, requester = %d\n", node_id, msg.payload[0], msg.payload[1], msg.requester);
            uint64_t va = msg.payload[0];
#ifdef BFS
            uint64_t val = msg.payload[1];
            if (data[(va - start) >> 2] != val)
                data[(va - start) >> 2] = val;
#endif
#ifdef PageRank
            double addend = *((double *)&msg.payload[1]);
            data[(va - start) >> 3] += addend;
#endif
#ifdef CC
            uint64_t val = msg.payload[1];
            // printf("start=%lx, va=%lx, val=%lx\n", start, va, val);
            data[(va - start) >> 2] = MIN(data[(va - start) >> 2], data[(val - start) >> 2]);
#endif
            SimGMMCoreMovType(GMM_CORE_DONE);
            SimGMMCoreMessage();

            // SimGMMCoreMovRecv(msg.requester);
            // SimGMMCoreMovRecv(msg.requester);
            // SimGMMCoreMovType(GMM_USER_DONE);
            // SimGMMCoreMovComponent(CORE);
            // SimGMMCoreMessage();
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

