#include "stdio.h"
#include "include/sim_api.h"
#include "stdint.h"

enum msg_t
{
   INVALID_MSG_TYPE = 0,
   MIN_MSG_TYPE,
   // Cache > tag directory
   EX_REQ = MIN_MSG_TYPE,
   SH_REQ,
   UPGRADE_REQ,
   INV_REQ,
   FLUSH_REQ,
   WB_REQ,
   SUB_REQ,
   CORE_REQ,
   // Tag directory > cache
   EX_REP,
   SH_REP,
   UPGRADE_REP,
   INV_REP,
   FLUSH_REP,
   WB_REP,
   SUB_REP,
   CORE_REP,
   NULLIFY_REQ,
   // Tag directory > DRAM
   DRAM_READ_REQ,
   DRAM_WRITE_REQ,
   // DRAM > tag directory
   DRAM_READ_REP,
   // GMM > TLB
   TLB_INSERT,

   MAX_MSG_TYPE = TLB_INSERT,
   NUM_MSG_TYPES = MAX_MSG_TYPE - MIN_MSG_TYPE + 1
};

#include "stdint.h"

struct GMMCoreMessage {
   int32_t policy;
   int32_t type;
   union {
      int32_t sender;
      int32_t receiver;
   };
   uint64_t payload[2];
};


int main()
{
   GMMCoreMessage msg;
   while (SimGMMCorePull(msg) != -1)
   {

      // printf("[GMM Core] Pull GMM message policy = %d, type = %d, sender = %d, addr = %lx\n",
      //        msg.policy, msg.type, msg.sender, msg.payload[0]);

      msg.type = TLB_INSERT;
      // msg.payload[0] = msg.payload[0] & ~((1UL << 20) - 1);
      // msg.payload[1] = msg.payload[1] + 1UL << 20;
      msg.payload[0] = 0x55555577cca0;
      msg.payload[1] = msg.payload[0] + 2832;
      // printf("[GMM Core] Send GMM message type = %d, start = %lx, end = %lx\n", 
      //         msg.type, msg.payload[0], msg.payload[1]);
      asm volatile("mfence" : : : "memory");
      SimGMMCoreMessage(msg);
   }


   return 0;
}
