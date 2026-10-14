/*************************************************************************
 * Copyright (c) 2026 BAAI. All rights reserved.
 ************************************************************************/

#ifndef FLAGCX_ILUVATAR_COMM_TRAITS_H_
#define FLAGCX_ILUVATAR_COMM_TRAITS_H_

#include "default_comm_traits.h"

// Iluvatar reuses the Default backend and replaces one operation. Everything
// else, including Comm, Window, Team, Barrier and the rest of Net, is
// inherited unchanged. flagcxDevNet derives from DeviceAPI::Net, so overriding
// the member here is enough to reach every caller without touching the shared
// Default traits.
struct IluvatarCommTraits : CommTraits<DefaultBackend<IluvatarPlatform>> {
  using Base = CommTraits<DefaultBackend<IluvatarPlatform>>;

  struct Net : Base::Net {
    FLAGCX_DEVICE_INLINE_DECORATOR
    Net(const Base::Comm &dc, int contextIndex) : Base::Net(dc, contextIndex) {}

    // A cross-node counter increment is applied by the host proxy as a
    // fetch_add on the host-mapped proxy word. ivcore11 has no system fence,
    // so the inherited resetProxy store of 0 is not ordered against increments
    // still in flight, and a later device load can still observe the previous
    // value. Drain this context's FIFO first, then publish 0 with a 32-bit
    // atomic exchange on the low half (the high half stays 0) and do not
    // return until this thread observes 0.
    FLAGCX_DEVICE_INLINE_DECORATOR void
    resetCounter(flagcxDevCounter_t counterId) const {
      int idx = this->_contextId * this->counterCount + (int)counterId;
      Base::Atomic::store(&this->_dc.getCompletionCounterBuffer()[idx],
                          Base::CompletionWord{0},
                          flagcxDeviceMemoryOrderRelease);
      if (this->fifoBuffer != nullptr)
        (void)Base::fifoFlush(this->fifoBuffer);
      if (this->counterBuffer == nullptr)
        return;
      uint32_t *low = reinterpret_cast<uint32_t *>(this->counterBuffer + idx);
      int iter = 0;
      for (;;) {
        Base::Atomic::exchange(low, 0u, flagcxDeviceMemoryOrderSeqCst);
        if (this->loadCounterValue(idx, flagcxDeviceMemoryOrderAcquire) ==
            Base::CompletionWord{0})
          break;
        if (this->fifoBuffer != nullptr &&
            Base::fifoTerminalStatus(
                reinterpret_cast<uint64_t *>(this->fifoBuffer)) !=
                flagcxSuccess)
          break;
        Base::Intrin::spinBackoff(iter++);
      }
    }
  };
};

using DeviceAPI = IluvatarCommTraits;

#endif // FLAGCX_ILUVATAR_COMM_TRAITS_H_
