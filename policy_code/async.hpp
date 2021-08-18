#include "stdio.h"
#include "include/sim_api.h"
#include "stdint.h"
#include <cassert>

#include "policy.hpp"
#include "msg.h"

#define INVALID_ADDR ((uint64_t)-1)

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

class Migration : public Policy
{

   uint64_t start, end, length;
   const uint64_t block_logsize = 20;
   const uint64_t block_size = 1UL << block_logsize;
   const uint64_t block_mask = ~(block_size - 1);
   const uint64_t num_nodes = 4;
   const uint64_t app_cores = 32;
   const float mem_cap = 1.;
   // const int cache_nodes1[4] = {19, 18, 17, 16};
   const int cache_nodes1[4] = {35, 34, 33, 32};

   bool *block_map;
   uint64_t node_id = -1;
   uint64_t cache_blocks = 0;
   uint64_t max_cache_blocks = 0;

   int get_home(uint64_t block_num)
   {
      return (block_num & (num_nodes - 1)) + app_cores;
   }

public:
   /* Hander for MemMsg from user core to GMM core */
   void USER_MSG_Handler()
   {
      switch (msg.subtype)
      {
         case PHASE_READ:       /* Change to read phase   */
         case PHASE_UPDATE:     /* Change to update phase */
            /* Broadcast phase change messages to all GMM cores */
            for (int node: subscribers)
            {
               SimGMMCoreMovType(GMM_MSG);
               SimGMMCoreMovReceiver(node);
               SimGMMCoreMessage();
            }
            break;
         case ATOMIC_UPDATE:    /* Async MemMsg to update data */
         {
            /* Forward msg to home node of the address */
            int home_node = get_home(msg.address);
            SimGMMCoreMovType(GMM_MSG);
            SimGMMCoreMovReceiver(home_node);
            SimGMMCoreMessage();
            break;
         }

      }
   }
   /* Handler for MemMsg from GMM core to GMM core */
   void GMM_MSG_Handler()
   {
      switch (msg.subtype)
      {
         case PHASE_READ:       /* Change to read phase   */
         {
            /* Replicate my chunk of data on all NUMA nodes with DMA */
            for (int node: subscribers)
            {
               SimGMMCoreMovType(DMA_REQ);
               SimGMMCoreMovSubtype(INIT);
               SimGMMCoreMovPayload1(paddr_begin[my_node_id][my_node_id]);
               SimGMMCoreMovPayload2(paddr_begin[node][my_node_id]);
               SimGMMCoreMovReceiver(my_node_id);
               SimGMMCoreMessage();
            }
            break;
         }
         case PHASE_UPDATE:     /* Change to update phase */
         {
            /* Flush user TLB */
            SimGMMCoreMovType(TLB_REQ);
            SimGMMCoreMovSubtype(TLB_FLUSH);
            SimGMMCoreMovReceiver(my_node_id);
            SimGMMCoreMessage();

            /* Update page tables */
            for (uint64_t vaddr = vaddr_begin; vaddr < vaddr_end; vaddr += PAGE_SIZE)
               page_table_disable(vaddr);

            /* Make sure page table updates go to main memory */
            SimGMMFlushCache();

            /* Let the original GMM core that got the user message know we are done */
            SimGMMCoreMovType(GMM_MSG);
            SimGMMCoreMovSubtype(PHASE_READ_DONE);
            SimGMMCoreMovReceiver(orig_requester);
            SimGMMCoreMessage();
            break;
         }
         case PHASE_READ_DONE:  /* All subscriber nodes have finished phase changes */
         case PHASE_UPDATE_DONE:
            nodes_complete ++;
            if (nodes_complete == subscribers.size())
            {
               /* Complete the synchronous MemMsg from user core to resume application */
               SimGMMCoreMovType(USER_REP);
               SimGMMCoreMovReceiver(my_node_id);
               SimGMMCoreMessage();
               nodes_complete = 0;
            }
            break;
         case ATOMIC_UPDATE: /* Async MemMsg used to update data */
         {
            /* Execute application logic at home node */
            dtype *vaddr = (dtype *)msg.address;
            dtype addend = msg.payload[0];
            *vaddr += addend;
            break;
         }
      }
   }
   void DMA_REP_Handler()
   {
      SimGMMCoreMovType(TLB_REQ);
      SimGMMCoreMovSubtype(TLB_FLUSH);
      SimGMMCoreMovReceiver(my_node_id);
      SimGMMCoreMessage();

      for (uint64_t vaddr = vaddr_begin; vaddr < vaddr_end; vaddr += PAGE_SIZE)
         page_table_enable(vaddr);

      SimGMMFlushCache();

      SimGMMCoreMovType(GMM_MSG);
      SimGMMCoreMovSubtype(PHASE_READ_DONE);
      SimGMMCoreMovReceiver(orig_requester);
      SimGMMCoreMessage();
   }
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
            block_map = new bool[num_blocks];
            for (int i = 0; i < length / block_size + 1; i ++)
               block_map[i] = false;

            SimGMMCoreMovType(GMM_CORE_DONE);
            SimGMMCoreMessage();
            break;
         }
         case SH_REQ:
         case EX_REQ:
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
         default:
            break;
      }
   }
};
