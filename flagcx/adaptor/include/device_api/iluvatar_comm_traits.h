/*************************************************************************
 * Copyright (c) 2026 BAAI. All rights reserved.
 ************************************************************************/

#ifndef FLAGCX_ILUVATAR_COMM_TRAITS_H_
#define FLAGCX_ILUVATAR_COMM_TRAITS_H_

// A cross-node counter increment is a host proxy fetch_add on the host-mapped
// proxy word. ivcore11 has no system fence, so a later volatile store of 0
// from the device is not ordered with that fetch_add and is not what the
// following device load observes. Drain the FIFO, then publish 0 with a
// 32-bit atomic exchange on the low half (the high half stays 0) and do not
// return until this thread observes 0.
#define FLAGCX_ILUVATAR_COUNTER_RESET_DRAIN(idx)                               \
  do {                                                                         \
    if (fifoBuffer != nullptr)                                                 \
      (void)fifoFlush(fifoBuffer);                                             \
    if (counterBuffer != nullptr) {                                            \
      uint32_t *low =                                                          \
          reinterpret_cast<uint32_t *>(counterBuffer + (idx));                 \
      int iter = 0;                                                            \
      do {                                                                     \
        Atomic::exchange(low, 0u, flagcxDeviceMemoryOrderSeqCst);             \
        if (loadCounterValue((idx), flagcxDeviceMemoryOrderAcquire) ==        \
            CompletionWord{0})                                                 \
          break;                                                               \
        if (fifoBuffer != nullptr &&                                           \
            fifoTerminalStatus((uint64_t *)fifoBuffer) != flagcxSuccess)       \
          break;                                                               \
        Intrin::spinBackoff(iter++);                                           \
      } while (1);                                                             \
    }                                                                          \
  } while (0)

#include "default_comm_traits.h"

using DeviceAPI = CommTraits<DefaultBackend<IluvatarPlatform>>;

#endif // FLAGCX_ILUVATAR_COMM_TRAITS_H_
