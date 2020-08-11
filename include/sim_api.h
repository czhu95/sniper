#ifndef __SIM_API
#define __SIM_API


#define SIM_CMD_ROI_TOGGLE        0  // Deprecated, for compatibility with programs compiled long ago
#define SIM_CMD_ROI_START         1
#define SIM_CMD_ROI_END           2
#define SIM_CMD_MHZ_SET           3
#define SIM_CMD_MARKER            4
#define SIM_CMD_USER              5
#define SIM_CMD_INSTRUMENT_MODE   6
#define SIM_CMD_MHZ_GET           7
#define SIM_CMD_IN_SIMULATOR      8
#define SIM_CMD_PROC_ID           9
#define SIM_CMD_THREAD_ID         10
#define SIM_CMD_NUM_PROCS         11
#define SIM_CMD_NUM_THREADS       12
#define SIM_CMD_NAMED_MARKER      13
#define SIM_CMD_SET_THREAD_NAME   14
#define SIM_CMD_GMM_CORE_MESSAGE  15
#define SIM_CMD_GMM_CORE_PULL     16
#define SIM_CMD_GMM_MOV_TYPE      17
#define SIM_CMD_GMM_MOV_COMPONENT 18
#define SIM_CMD_GMM_MOV_RECV      19
#define SIM_CMD_GMM_MOV_PAYLOAD1  20
#define SIM_CMD_GMM_MOV_PAYLOAD2  21
#define SIM_CMD_GMM_MOV_PAYLOAD   22

#define SIM_OPT_INSTRUMENT_DETAILED    0
#define SIM_OPT_INSTRUMENT_WARMUP      1
#define SIM_OPT_INSTRUMENT_FASTFORWARD 2

#if defined(ARM_64)

#define SimMagic0(cmd) ({                       \
   unsigned long _cmd = (cmd), _res;            \
   asm volatile (           \
   "mov x1, %[x]\n"         \
   "\tbfm x0, x0, 0, 0\n"   \
   : [ret]"=r"(_res)        \
   : [x]"r"(_cmd)           \
   );                       \
})

#define SimMagic1(cmd, arg0) ({              \
   unsigned long _cmd = (cmd), _arg0 = (arg0), _res; \
   asm volatile (           \
   "mov x1, %[x]\n"         \
   "\tmov x2, %[y]\n"       \
   "\tbfm x0, x0, 0, 0\n"   \
   : [ret]"=r"(_res)        \
   : [x]"r"(_cmd),          \
     [y]"r"(_arg0)          \
   : "x2", "x1"                   \
   );                       \
})

#define SimMagic2(cmd, arg0, arg1) ({        \
   unsigned long _cmd = (cmd), _arg0 = (arg0), _arg1 = (arg1), _res; \
   asm volatile (           \
   "mov x1, %[x]\n"         \
   "\tmov x2, %[y]\n"       \
   "\tmov x3, %[z]\n"       \
   "\tbfm x0, x0, 0, 0\n"   \
   : [ret]"=r"(_res)        \
   : [x]"r"(_cmd),          \
     [y]"r"(_arg0),          \
     [z]"r"(_arg1)          \
   : "x1", "x2", "x3"                   \
   );                       \
})

#else  // end ARM_64

#if defined(__i386)
   #define MAGIC_REG_A "eax"
   #define MAGIC_REG_B "edx" // Required for -fPIC support
   #define MAGIC_REG_C "ecx"
#else
   #define MAGIC_REG_A "rax"
   #define MAGIC_REG_B "rbx"
   #define MAGIC_REG_C "rcx"
#endif

#define SimMagic0(cmd) ({                    \
   unsigned long _cmd = (cmd), _res;         \
   __asm__ __volatile__ (                    \
   "mov %1, %%" MAGIC_REG_A "\n"             \
   "\txchg %%bx, %%bx\n"                     \
   : "=a" (_res)           /* output    */   \
   : "g"(_cmd)             /* input     */   \
      );                   /* clobbered */   \
   _res;                                     \
})

#define SimMagic1(cmd, arg0) ({              \
   unsigned long _cmd = (cmd), _arg0 = (arg0), _res; \
   __asm__ __volatile__ (                    \
   "mov %1, %%" MAGIC_REG_A "\n"             \
   "\tmov %2, %%" MAGIC_REG_B "\n"           \
   "\txchg %%bx, %%bx\n"                     \
   : "=a" (_res)           /* output    */   \
   : "g"(_cmd),                              \
     "g"(_arg0)            /* input     */   \
   : "%" MAGIC_REG_B );    /* clobbered */   \
   _res;                                     \
})

#define SimMagic2(cmd, arg0, arg1) ({        \
   unsigned long _cmd = (cmd), _arg0 = (arg0), _arg1 = (arg1), _res; \
   __asm__ __volatile__ (                    \
   "mov %1, %%" MAGIC_REG_A "\n"             \
   "\tmov %2, %%" MAGIC_REG_B "\n"           \
   "\tmov %3, %%" MAGIC_REG_C "\n"           \
   "\txchg %%bx, %%bx\n"                     \
   : "=a" (_res)           /* output    */   \
   : "g"(_cmd),                              \
     "g"(_arg0),                             \
     "g"(_arg1)            /* input     */   \
   : "%" MAGIC_REG_B, "%" MAGIC_REG_C ); /* clobbered */ \
   _res;                                     \
})

#endif

#define SimRoiStart()             SimMagic0(SIM_CMD_ROI_START)
#define SimRoiEnd()               SimMagic0(SIM_CMD_ROI_END)
#define SimGetProcId()            SimMagic0(SIM_CMD_PROC_ID)
#define SimGetThreadId()          SimMagic0(SIM_CMD_THREAD_ID)
#define SimSetThreadName(name)    SimMagic1(SIM_CMD_SET_THREAD_NAME, (unsigned long)(name))
#define SimGetNumProcs()          SimMagic0(SIM_CMD_NUM_PROCS)
#define SimGetNumThreads()        SimMagic0(SIM_CMD_NUM_THREADS)
#define SimSetFreqMHz(proc, mhz)  SimMagic2(SIM_CMD_MHZ_SET, proc, mhz)
#define SimSetOwnFreqMHz(mhz)     SimSetFreqMHz(SimGetProcId(), mhz)
#define SimGetFreqMHz(proc)       SimMagic1(SIM_CMD_MHZ_GET, proc)
#define SimGetOwnFreqMHz()        SimGetFreqMHz(SimGetProcId())
#define SimMarker(arg0, arg1)     SimMagic2(SIM_CMD_MARKER, arg0, arg1)
#define SimNamedMarker(arg0, str) SimMagic2(SIM_CMD_NAMED_MARKER, arg0, (unsigned long)(str))
#define SimUser(cmd, arg)         SimMagic2(SIM_CMD_USER, cmd, arg)
#define SimSetInstrumentMode(opt) SimMagic1(SIM_CMD_INSTRUMENT_MODE, opt)
#define SimInSimulator()          (SimMagic0(SIM_CMD_IN_SIMULATOR)!=SIM_CMD_IN_SIMULATOR)

#define SimGMMCoreMessage()                 SimMagic0(SIM_CMD_GMM_CORE_MESSAGE)
#define SimGMMCorePull(msg)                 SimMagic1(SIM_CMD_GMM_CORE_PULL, (unsigned long)&msg)
#define SimGMMCoreMovType(type)             SimMagic1(SIM_CMD_GMM_MOV_TYPE, type)
#define SimGMMCoreMovComponent(component)   SimMagic1(SIM_CMD_GMM_MOV_COMPONENT, component)
#define SimGMMCoreMovRecv(recv)             SimMagic1(SIM_CMD_GMM_MOV_RECV, recv)
#define SimGMMCoreMovPayload1(payload)      SimMagic1(SIM_CMD_GMM_MOV_PAYLOAD1, payload)
#define SimGMMCoreMovPayload2(payload)      SimMagic1(SIM_CMD_GMM_MOV_PAYLOAD2, payload)
#define SimGMMCoreMovPayload(p1, p2)        SimMagic2(SIM_CMD_GMM_MOV_PAYLOAD, p1, p2)


#endif /* __SIM_API */
