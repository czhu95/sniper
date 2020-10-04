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

class AtomicSwap : public Policy
{
   uint64_t start, end, length;
   const uint64_t block_logsize = 20;
   const uint64_t block_size = 1UL << block_logsize;
   const uint64_t block_mask = ~(block_size - 1);
   const uint64_t num_nodes = 4;
   const uint64_t app_cores = 16;
   const uint64_t shared_cores = app_cores / num_nodes;
   const float mem_cap = 4. / num_nodes;

   bool *block_map; // [length / block_size + 1];
   uint64_t node_id = -1;
   uint64_t checksum;
   uint64_t cache_blocks = 0;
   uint64_t max_cache_blocks = 0;

   int get_home(uint64_t block_num)
   {
      return (block_num & (num_nodes - 1)) + app_cores;
   }

   struct swap_t
   {
       uint64_t addr1;
       uint64_t addr2;
       uint64_t val1;
       uint64_t val2;
       uint64_t requester;
   };

   swap_t swaps[MAX_REQ];

   int head = 0, tail = 0;

   inline void insert_swap(uint64_t requester, uint64_t addr1, uint64_t addr2, bool &fetch1, bool &fetch2)
   {
       int i = head;
       fetch1 = true;
       fetch2 = true;
       while (i != tail)
       {
          if (swaps[i].addr1 == addr1 || swaps[i].addr2 == addr1)
             fetch1 = false;
          if (swaps[i].addr1 == addr2 || swaps[i].addr2 == addr2)
             fetch2 = false;

          i = (i + 1) & (MAX_REQ - 1);
       }
       // printf("[GMM Core: %d] free=%d\n", node_id, free);
       assert(tail != -1 && tail != MAX_REQ);
       swaps[tail].addr1 = addr1;
       swaps[tail].addr2 = addr2;
       swaps[tail].val1 = 0;
       swaps[tail].val2 = 0;
       swaps[tail].requester = requester;

       tail = (tail + 1) & (MAX_REQ - 1);
       assert(tail != head);

       // printf("[GMM Core: %d] inserted swap %lx <-> %lx, slot=%d\n", node_id, addr1, addr2, free);
   }

public:
   AtomicSwap()
   {
      for (int i = 0; i < MAX_REQ; i ++) {
         swaps[i].addr1 = INVALID_ADDR;
         swaps[i].addr2 = INVALID_ADDR;
      }
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
            max_cache_blocks = int(num_blocks * mem_cap);
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
               if (node == node_id || cache_blocks < max_cache_blocks)
               {
                  cache_blocks ++;
                  node = node_id;
               }
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
            SimGMMCoreMovType(ATOMIC_UPDATE_MSG);
            SimGMMCoreMovComponent(GMM_CORE);
            SimGMMCoreMessage();
         }
         case ATOMIC_UPDATE_MSG:
         {
            uint64_t addr1 = msg.payload[0];
            uint64_t addr2 = msg.payload[1];
            bool fetch1, fetch2;
            insert_swap(msg.requester, addr1, addr2, fetch1, fetch2);

            SimGMMCoreMovRecv(node_id);
            SimGMMCoreMovType(USER_CACHE_READ_REQ);
            SimGMMCoreMovComponent(LAST_LEVEL_CACHE);

            if (fetch1)
            {
               SimGMMCoreMovPayload1(addr1);
               SimGMMCoreMovPayload1(addr1);
               SimGMMCoreMessage();
            }

            if (fetch2)
            {
               SimGMMCoreMovPayload1(addr2);
               SimGMMCoreMovPayload1(addr2);
               SimGMMCoreMessage();
            }

            if (!fetch1 && !fetch2)
            {
               SimGMMCoreMovType(GMM_CORE_DONE);
               SimGMMCoreMessage();
            }
            break;
         }
         case USER_CACHE_READ_REP:
         {
            // printf("[GMM Core: %d] received cache data %lx = %lx\n", node_id, msg.payload[0], msg.payload[1]);
            int i = head;
            uint64_t addr = msg.payload[0];
            while (i != tail)
            {
               if (swaps[i].addr1 == addr)
               {
                  // printf("[GMM Core: %d] updated slot %d val1 = %lx\n", node_id, i, msg.payload[1]);
                  swaps[i].val1 = msg.payload[1];
                  break;
               }
               if (swaps[i].addr2 == addr)
               {
                  // printf("[GMM Core: %d] updated slot %d val2 = %lx\n", node_id, i, msg.payload[1]);
                  swaps[i].val2 = msg.payload[1];
                  break;
               }

               i = (i + 1) & (MAX_REQ - 1);
            }

            assert(i != tail);

            bool complete_any = false;

            while (swaps[head].val1 && swaps[head].val2 && head != tail)
            {
               complete_any = true;
               // printf("[GMM Core: %d] Sending cache write req, requester = %d\n", node_id, msg.requester);
               SimGMMCoreMovRecv(node_id);
               SimGMMCoreMovType(USER_CACHE_WRITE_REQ);
               SimGMMCoreMovComponent(LAST_LEVEL_CACHE);
               SimGMMCoreMovPayload1(swaps[head].addr1);
               SimGMMCoreMovPayload2(swaps[head].val2);
               SimGMMCoreMessage();

               SimGMMCoreMovPayload1(swaps[head].addr2);
               SimGMMCoreMovPayload2(swaps[head].val1);
               SimGMMCoreMessage();

               if (swaps[head].requester / shared_cores == node_id - app_cores)
               {
                  // printf("[GMM Core: %d] Waking user core %d\n", node_id, msg.requester);
                  SimGMMCoreMovRecv(swaps[head].requester);
                  SimGMMCoreMovRecv(swaps[head].requester);
                  SimGMMCoreMovType(GMM_USER_DONE);
                  SimGMMCoreMovComponent(CORE);
                  SimGMMCoreMessage();
               }

               bool forward1 = false, forward2 = false;
               for (int j = (head + 1) & (MAX_REQ - 1); j != tail; )
               {
                   if (!forward1 && swaps[j].addr1 == swaps[head].addr1)
                   {
                      assert(swaps[j].val1 == 0);
                      swaps[j].val1 = swaps[head].val2;
                      forward1 = true;
                   }

                   if (!forward2 && swaps[j].addr1 == swaps[head].addr2)
                   {
                      assert(swaps[j].val1 == 0);
                      swaps[j].val1 = swaps[head].val1;
                      forward2 = true;
                   }

                   if (!forward1 && swaps[j].addr2 == swaps[head].addr1)
                   {
                      assert(swaps[j].val2 == 0);
                      swaps[j].val2 = swaps[head].val2;
                      forward1 = true;
                   }

                   if (!forward2 && swaps[j].addr2 == swaps[head].addr2)
                   {
                      assert(swaps[j].val2 == 0);
                      swaps[j].val2 = swaps[head].val1;
                      forward2 = true;
                   }
                   j = (j + 1) & (MAX_REQ - 1);
               }
               head = (head + 1) & (MAX_REQ - 1);
            }

            if (!complete_any)
            {
               SimGMMCoreMovType(GMM_CORE_DONE);
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

