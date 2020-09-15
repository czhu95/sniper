#include "include/sim_api.h"
#include "atomic_swap.hpp"
#include "atomic_writeadd.hpp"
#include "replication.hpp"

int main()
{
   Policy *policies[16] = {NULL};
   // AtomicSwap atomic_swap_policy;
   // Replication replication_policy;
   GMMCoreMessage msg;

   // for (auto &b : block_map)
   //    b = false;

   // unsigned long pull = SimGMMCorePull(msg);
   // printf("%lu\n", pull);
   while (SimGMMCorePull(msg) != -1)
   {
      asm volatile("" : : : "memory");
      // printf("type = %d\n", msg.type);
      switch (msg.policy)
      {
         case 1 /*replication*/:
            if (msg.type == POLICY_INIT)
                policies[msg.segid] = new Replication();
            ((Replication *)policies[msg.segid])->Exec(msg);
            // replication_policy.Exec(msg);
            break;
         case 2 /*atomic_writeadd*/:
            if (msg.type == POLICY_INIT)
                policies[msg.segid] = new AtomicWriteAdd();
            ((AtomicWriteAdd *)policies[msg.segid])->Exec(msg);
         case 4 /*atomic_swap*/:
            if (msg.type == POLICY_INIT)
                policies[msg.segid] = new AtomicSwap();
            ((AtomicSwap *)policies[msg.segid])->Exec(msg);

            // atomic_swap_policy.Exec(msg);
            break;
         default:
            break;
      }
   }
   return 0;
}
