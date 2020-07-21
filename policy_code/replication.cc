#include "stdio.h"
#include "include/sim_api.h"
#include "stdint.h"

#include "policy_code/msg.h"

// const uint64_t start = 0x55555ee516f0;
// const uint64_t length = 40000000;
uint64_t start, end, length;
const uint64_t block_logsize = 20;
const uint64_t block_size = 1UL << block_logsize;
const uint64_t block_mask = ~(block_size - 1);

bool *block_map;
int node_id = -1;

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

int main()
{
   GMMCoreMessage msg;
   uint64_t addr;
   int block_num;
   uint64_t checksum;

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
            break;
         case SH_REQ:
            // msg.payload[0] = msg.payload[0] & ~((1UL << 20) - 1);
            // msg.payload[1] = msg.payload[1] + 1UL << 20;
            // printf("here\n");
            addr = msg.payload[0];
            // printf("addr: %lx\n", addr);
            block_num = (addr >> block_logsize) - (start >> block_logsize);
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
         default:
            break;
      }

      // printf("[GMM Core] Pull GMM message policy = %d, type = %d, sender = %d, addr = %lx\n",
      //        msg.policy, msg.type, msg.sender, msg.payload[0]);

   }
   return 0;
}
