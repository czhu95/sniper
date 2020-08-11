#include "include/sim_api.h"
#include "atomic_swap.hpp"
#include "replication.hpp"

int main()
{
   AtomicSwap atomic_swap_policy;
   Replication replication_policy;
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
            replication_policy.Exec(msg);
            break;
         case 4 /*atomic_swap*/:
            atomic_swap_policy.Exec(msg);
            break;
         default:
            break;
      }
   }
   return 0;
}
