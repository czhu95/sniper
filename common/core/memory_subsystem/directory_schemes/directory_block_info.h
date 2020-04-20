#ifndef __DIRECTORY_BLOCK_INFO_H__
#define __DIRECTORY_BLOCK_INFO_H__

#include "directory_state.h"
#include "subsecond_time.h"

class DirectoryBlockInfo
{
   private:
      DirectoryState::dstate_t m_dstate;
      SubsecondTime m_slme_available;

   public:
      DirectoryBlockInfo(
            DirectoryState::dstate_t dstate = DirectoryState::UNCACHED):
         m_dstate(dstate),
         m_slme_available(SubsecondTime::MaxTime())
      {}
      ~DirectoryBlockInfo() {}

      DirectoryState::dstate_t getDState() { return m_dstate; }
      void setDState(DirectoryState::dstate_t dstate) { m_dstate = dstate; }

      SubsecondTime getSLMeAvailable() { return m_slme_available; }
      void setSLMeAvailable(SubsecondTime time) { m_slme_available = time; }

};

#endif /* __DIRECTORY_BLOCK_INFO_H__ */
