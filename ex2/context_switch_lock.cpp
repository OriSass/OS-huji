//
// Created by orisa on 28/04/2025.
//

#include <csignal>
#include <iostream>
#include "context_switch_lock.h"
// ========================
// Internal Global Variables
// ========================

context_switch_lock::context_switch_lock() {
    disable_context_switch ();
  }
  context_switch_lock::~context_switch_lock() {
    enable_context_switch ();
  }


  void context_switch_lock::disable_context_switch()
  {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    if (-1 == sigprocmask(SIG_BLOCK, &sigset, nullptr))
    {
      std::cerr <<"system error: failed to disable context switch";
      exit(1);
    }
  }

  void context_switch_lock::enable_context_switch()
  {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGVTALRM);
    if (-1 == sigprocmask(SIG_UNBLOCK, &sigset, nullptr))
    {
      std::cerr <<"system error: failed to enable context switch";
      exit(1);
    }
  }
