#include "dynamic_micro_op.h"
#include "log.h"
#include "core_model.h"
#include "sift_format.h"

DynamicMicroOp::DynamicMicroOp(const MicroOp *uop, const CoreModel *core_model, ComponentPeriod period)
   : m_uop(uop)
   , m_core_model(core_model)
   , m_period(period)
{
   LOG_ASSERT_ERROR(period != SubsecondTime::Zero(), "MicroOp Period is == SubsecondTime::Zero()");

   this->squashed = false;

   this->branchTaken = false;
   this->branchMispredicted = false;

   this->intraInstructionDependencies = uop->intraInstructionDependencies;
   this->microOpTypeOffset = uop->microOpTypeOffset;
   this->squashedCount = 0;
   this->dependenciesLength = 0;

   this->execLatency = m_core_model->getInstructionLatency(uop);

   this->sequenceNumber = INVALID_SEQNR;

   this->dCacheHitWhere = HitWhere::UNKNOWN;
   this->iCacheHitWhere = HitWhere::L1I; // Default to an icache hit
   this->iCacheLatency = 0;

   this->m_forceLongLatencyLoad = false;

   for(uint32_t i = 0 ; i < MAXIMUM_NUMBER_OF_DEPENDENCIES; i++)
      this->dependencies[i] = -1;

   LOG_ASSERT_ERROR(m_uop != NULL, "uop is NULL");

   first = m_uop->isFirst();
   last = m_uop->isLast();

   this->m_core_msg = NULL;
   this->m_user_msg = NULL;
}


DynamicMicroOp::DynamicMicroOp(const DynamicMicroOp *other)
   : m_uop(other->m_uop)
   , m_core_model(other->m_core_model)
   , m_period(other->m_period)
   , sequenceNumber(INVALID_SEQNR)
   , address(other->address)
   , squashed(other->squashed)
   , intraInstructionDependencies(other->intraInstructionDependencies)
   , microOpTypeOffset(other->microOpTypeOffset)
   , squashedCount(other->squashedCount)
   , dependenciesLength(0)
   , execLatency(other->execLatency)
   , branchTaken(false)
   , branchMispredicted(false)
   , dCacheHitWhere(HitWhere::UNKNOWN)
   , iCacheHitWhere(HitWhere::L1I)
   , iCacheLatency(0)
   , m_forceLongLatencyLoad(false)
   , first(other->first)
   , last(other->last)
   , m_user_thread_id(other->m_user_thread_id)
{
   m_user_msg = other->m_user_msg ? new Sift::GMMUserMessage(*(other->m_user_msg)) : NULL;
   m_core_msg = other->m_core_msg ? new Sift::GMMCoreMessage(*(other->m_core_msg)) : NULL;

   for(uint32_t i = 0 ; i < MAXIMUM_NUMBER_OF_DEPENDENCIES; i++)
      this->dependencies[i] = -1;
}

DynamicMicroOp::~DynamicMicroOp()
{
   if (m_core_msg)
      delete m_core_msg;

   if (m_user_msg)
      delete m_user_msg;
}

void DynamicMicroOp::squash(std::vector<DynamicMicroOp*>* array)
{
   squashed = true;

   if (array)
   {
      // Fix up isFirst/isLast after possibly squashing the first/last microop in a list
      for(int index = 0; index < (int)array->size(); ++index)
         if (!(*array)[index]->isSquashed())
         {
            (*array)[index]->setFirst(true);
            break;
         }
      for(int index = array->size() - 1; index >= 0; --index)
         if (!(*array)[index]->isSquashed())
         {
            (*array)[index]->setLast(true);
            break;
         }
   }
}

uint64_t DynamicMicroOp::getDependency(uint32_t index) const
{
   if (index < this->intraInstructionDependencies) {
      return this->sequenceNumber - this->microOpTypeOffset - this->intraInstructionDependencies + index;
   } else {
      assert((index >= this->intraInstructionDependencies) && ((index - this->intraInstructionDependencies) < this->dependenciesLength));
      return this->dependencies[index - this->intraInstructionDependencies];
   }
}

void DynamicMicroOp::addDependency(uint64_t sequenceNumber)
{
   if (!Tools::contains(dependencies, dependenciesLength, sequenceNumber)) {
      assert(this->dependenciesLength < MAXIMUM_NUMBER_OF_DEPENDENCIES);
      dependencies[dependenciesLength] = sequenceNumber;
      dependenciesLength++;
   }
}

void DynamicMicroOp::removeDependency(uint64_t sequenceNumber)
{
   if (sequenceNumber >= this->sequenceNumber - this->microOpTypeOffset - this->intraInstructionDependencies) {
      // Intra-instruction dependency
      while(intraInstructionDependencies && !(sequenceNumber == this->sequenceNumber - this->microOpTypeOffset - this->intraInstructionDependencies)) {
         // Remove the first intra-instruction dependency, but since this is not the one to be removed, add it to the regular dependencies list
         dependencies[dependenciesLength] = this->sequenceNumber - this->microOpTypeOffset - this->intraInstructionDependencies;
         dependenciesLength++;
         LOG_ASSERT_ERROR(dependenciesLength < MAXIMUM_NUMBER_OF_DEPENDENCIES, "dependenciesLength(%u) > MAX(%u)", dependenciesLength, MAXIMUM_NUMBER_OF_DEPENDENCIES);
         intraInstructionDependencies--;
      }
      // Make sure the exit condition was that the dependency to be removed is now the first one, not that we have exhausted the list
      LOG_ASSERT_ERROR(intraInstructionDependencies > 0, "Something went wrong while removing an intra-instruction dependency");
      // Remove the first intra-instruction dependency by decrementing intraInstructionDependencies
      intraInstructionDependencies--;
   } else {
      // Inter-instruction dependency
      LOG_ASSERT_ERROR(dependenciesLength > 0, "Cannot remove dependency when there are none");
      if (dependencies[dependenciesLength-1] == sequenceNumber)
         ; // sequenceNumber to remove is already at the end, we can just decrement dependenciesLength
      else {
         // Move sequenceNumber to the end of the list
         uint64_t idx = Tools::index(dependencies, dependenciesLength, sequenceNumber);
         LOG_ASSERT_ERROR(idx != UINT64_MAX, "MicroOp dependency list does not contain %ld", sequenceNumber);
         Tools::swap(dependencies, idx, dependenciesLength-1);
      }
      dependenciesLength--;
   }
}

const Memory::Access& DynamicMicroOp::getLoadAccess() const
{
   assert(this->getMicroOp()->isLoad());
   return this->address;
}

const Memory::Access& DynamicMicroOp::getStoreAccess() const
{
   assert(this->getMicroOp()->isStore());
   return this->address;
}

bool DynamicMicroOp::isLongLatencyLoad() const
{
   LOG_ASSERT_ERROR(getMicroOp()->isLoad(), "Expected a load instruction.");

   uint32_t cutoff = m_core_model->getLongLatencyCutoff();

   // If we are enabled, indicate that this is a long latency load if the latency
   // is above a certain cutoff value
   // Also, honor the forceLLL request if indicated
   return (m_forceLongLatencyLoad || ((cutoff > 0) && (this->execLatency > cutoff)));
}

void DynamicMicroOp::attachGMMCoreMessage(Sift::GMMCoreMessage *msg)
{
   LOG_ASSERT_ERROR(getMicroOp()->isGMMCoreMsg(), "Expected a gmm core instruction.");
   LOG_ASSERT_ERROR(m_core_msg == NULL, "Already has core message attached.");
   m_core_msg = msg;
}

void DynamicMicroOp::attachGMMUserMessage(Sift::GMMUserMessage *msg)
{
   LOG_ASSERT_ERROR(getMicroOp()->isGMMUser(), "Expected a gmm user instruction.");
   LOG_ASSERT_ERROR(m_user_msg == NULL, "Already has user message attached.");
   m_user_msg = msg;
}

Sift::GMMCoreMessage *DynamicMicroOp::getGMMCoreMessage() const
{
   return m_core_msg;
}

Sift::GMMUserMessage *DynamicMicroOp::getGMMUserMessage() const
{
   return m_user_msg;
}

void DynamicMicroOp::setUserThreadId(UInt64 user_thread_id)
{
   m_user_thread_id = user_thread_id;
}

UInt64 DynamicMicroOp::getUserThreadId() const
{
   return m_user_thread_id;
}
