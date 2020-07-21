#include "stdio.h"
#include "include/sim_api.h"
#include "stdint.h"
#include <cassert>

#include "policy_code/msg.h"

#define MAX_REQ 16
#define INVALID_ADDR ((uint64_t)-1)

// const uint64_t start = 0x55555ee516f0;
// const uint64_t length = 40000000;
// const uint64_t start = 0x55555577cca0;
// const uint64_t length = 2832;
uint64_t start, end, length;
const uint64_t block_logsize = 20;
const uint64_t block_size = 1UL << block_logsize;
const uint64_t block_mask = ~(block_size - 1);
const uint64_t num_nodes = 2;
const uint64_t home_node = 8;

bool *block_map; // [length / block_size + 1];
int node_id = -1;

struct update_req
{
   uint64_t addr;
   int32_t cnt;
};

update_req reqs[MAX_REQ];

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

int main()
{
   GMMCoreMessage msg;
   uint64_t checksum;
   for (int i = 0; i < MAX_REQ; i ++) {
      reqs[i].addr = INVALID_ADDR;
   }
   // for (auto &b : block_map)
   //    b = false;

   // unsigned long pull = SimGMMCorePull(msg);
   // printf("%lu\n", pull);
   while (SimGMMCorePull(msg) != -1)
   {
      asm volatile("" : : : "memory");
      // printf("type = %d\n", msg.type);
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

            msg.type = GMM_CORE_DONE;
            checksum = CHECKSUM(msg);
            SimGMMCoreMessage(msg, checksum);
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
               msg.type = TLB_INSERT;
               msg.component = GMM_CORE;
               msg.receiver = node_id;
               msg.payload[0] = MAX(addr & block_mask, start);
               msg.payload[1] = MIN((addr & block_mask) + block_size, end);
               checksum = CHECKSUM(msg);
               // printf("[GMM Core] Send GMM message type = %d, start = %lx, end = %lx\n", 
               //        msg.type, msg.payload[0], msg.payload[1]);
               SimGMMCoreMessage(msg, checksum);
            }
            break;
         }
         case ATOMIC_UPDATE_REQ:
         {
            // printf("[GMM Core: %d] received atomic update req @%lx", node_id, msg.payload[0]);
            int i;
            for (i = 0; i < MAX_REQ; i ++)
            {
               if (reqs[i].addr == INVALID_ADDR)
               {
                  reqs[i].addr = msg.payload[0];
                  reqs[i].cnt = num_nodes;
                  break;
               }
            }
            assert(i != MAX_REQ);
            msg.type = ATOMIC_UPDATE_MSG;
            msg.component = GMM_CORE;
            checksum = CHECKSUM(msg);
            SimGMMCoreMessage(msg, checksum);

            msg.type = INV_REQ;
            msg.component = LAST_LEVEL_CACHE;
            msg.receiver = node_id;
            checksum = CHECKSUM(msg);
            SimGMMCoreMessage(msg, checksum);
            break;
         }
         case ATOMIC_UPDATE_REP:
         {
            // printf("[GMM Core: %d] received atomic update rep @%lx\n", node_id, msg.payload[0]);
            int i;
            for (i = 0; i < MAX_REQ; i ++)
            {
               if (reqs[i].addr == msg.payload[0])
               {
                  reqs[i].cnt --;
                  // fprintf(stdout, "[GMM Core: %d] addr = %lx, cnt = %d\n", node_id, reqs[i].addr, reqs[i].cnt);
                  if (reqs[i].cnt == 0)
                  {
                     msg.type = GMM_USER_DONE;
                     msg.component = CORE;
                     msg.receiver = msg.requester;
                     checksum = CHECKSUM(msg);
                     SimGMMCoreMessage(msg, checksum);
                     reqs[i].addr = INVALID_ADDR;
                  }
                  else
                  {
                     msg.type = GMM_CORE_DONE;
                     checksum = CHECKSUM(msg);
                     SimGMMCoreMessage(msg, checksum);
                  }
                  break;
               }
            }
            if (i == MAX_REQ)
            {
               msg.type = GMM_CORE_DONE;
               checksum = CHECKSUM(msg);
               SimGMMCoreMessage(msg, checksum);
            }
               // fprintf(stdout, "[GMM Core: %d] addr = %lx, cannot find request.\n", node_id, msg.payload[0]);
            break;
         }
         case ATOMIC_UPDATE_MSG:
         {
            msg.type = INV_REQ;
            msg.component = LAST_LEVEL_CACHE;
            msg.receiver = node_id;
            checksum = CHECKSUM(msg);
            SimGMMCoreMessage(msg, checksum);
            break;
         }
         case INV_REP:
         {
            msg.type = ATOMIC_UPDATE_REP;
            msg.component = GMM_CORE;
            msg.receiver = home_node;
            checksum = CHECKSUM(msg);
            SimGMMCoreMessage(msg, checksum);
            break;
         }
         // case ATOMIC_UPDATE_DONE:
         // {
         //    msg.type = GMM_USER_DONE;
         //    msg.component = CORE;
         //    msg.receiver = msg.requester;
         //    uint64_t checksum = CHECKSUM(msg);
         //    SimGMMCoreMessage(msg, checksum);
         //    break;
         // }
         default:
            msg.type = GMM_CORE_DONE;
            checksum = CHECKSUM(msg);
            SimGMMCoreMessage(msg, checksum);
            break;
      }

      // printf("[GMM Core] Pull GMM message policy = %d, type = %d, sender = %d, addr = %lx\n",
      //        msg.policy, msg.type, msg.sender, msg.payload[0]);

   }
   return 0;
}
