#ifndef CONTEXT_SWITCH_LOCK_H
#define CONTEXT_SWITCH_LOCK_H

#include <csignal>
#include <iostream>

class context_switch_lock
{
 public:
  // Constructor: disables context switch upon creation
  context_switch_lock();

  // Destructor: enables context switch upon destruction
  ~context_switch_lock();

 private:
  // Disable context switch (blocking SIGVTALRM signal)
  static void disable_context_switch();

  // Enable context switch (unblocking SIGVTALRM signal)
  static void enable_context_switch();
};

#endif  // CONTEXT_SWITCH_LOCK_H
