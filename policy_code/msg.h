#ifndef MSG_H_
#define MSG_H_

enum msg_t
{
   INVALID_MSG_TYPE = 0,
   MIN_MSG_TYPE,
   // Cache > tag directory
   EX_REQ = MIN_MSG_TYPE,   /* 1 */
   SH_REQ,                  /* 2 */
   UPGRADE_REQ,             /* 3 */
   INV_REQ,                 /* 4 */
   FLUSH_REQ,
   WB_REQ,
   SUB_REQ,
   CORE_REQ,
   RP_REQ,
   ATOMIC_UPDATE_REQ,
   // Tag directory > cache
   EX_REP,                  /* 11 */
   SH_REP,
   UPGRADE_REP,
   INV_REP,
   FLUSH_REP,
   WB_REP,
   SUB_REP,
   CORE_REP,
   NULLIFY_REQ,
   RP_REP,
   // Tag directory > DRAM
   DRAM_READ_REQ,           /* 21 */
   DRAM_WRITE_REQ,
   // DRAM > tag directory
   DRAM_READ_REP,
   // GMM > TLB
   TLB_INSERT,
   // GMM > GMM
   ATOMIC_UPDATE_MSG,
   ATOMIC_UPDATE_REP,
   ATOMIC_UPDATE_DONE,

   USER_CACHE_READ_REQ,
   USER_CACHE_WRITE_REQ,
   USER_CACHE_READ_REP,
   USER_CACHE_WRITE_REP,

   POLICY_INIT,

   GMM_USER_DONE,
   GMM_CORE_DONE,

   MAX_MSG_TYPE = GMM_CORE_DONE,
   NUM_MSG_TYPES = MAX_MSG_TYPE - MIN_MSG_TYPE + 1
};

enum component_t
{
   INVALID_MEM_COMPONENT = 0,
   MIN_MEM_COMPONENT,
   CORE = MIN_MEM_COMPONENT,
   FIRST_LEVEL_CACHE,
   L1_ICACHE = FIRST_LEVEL_CACHE,
   L1_DCACHE,
   L2_CACHE,
   L3_CACHE,
   L4_CACHE,
   /* more, unnamed stuff follows.
      make sure that MAX_MEM_COMPONENT < 32 as pr_l2_cache_block_info.h contains a 32-bit bitfield of these things
   */
   LAST_LEVEL_CACHE = 20,
   TAG_DIR,
   GMM_CORE,
   GMM_CACHE,
   NUCA_CACHE,
   DRAM_CACHE,
   DRAM,
   MAX_MEM_COMPONENT = DRAM,
   NUM_MEM_COMPONENTS = MAX_MEM_COMPONENT - MIN_MEM_COMPONENT + 1
};


struct GMMCoreMessage {
   int16_t requester;
   int16_t policy;
   int16_t type;
   int16_t component;
   int32_t sender;
   int32_t receiver;
   uint64_t payload[2];
};

// #define CHECKSUM(msg)                       \
// ({                                          \
//    uint64_t *regs = (uint64_t *)&msg;       \
//    regs[0] ^ regs[1] ^ regs[2] ^ regs[3];   \
// })

#endif
