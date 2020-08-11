#ifndef POLICY_H_
#define POLICY_H_

#include "msg.h"
#include "include/sim_api.h"

class Policy
{
public:
   virtual void Exec(GMMCoreMessage &msg) = 0;
};
#endif
