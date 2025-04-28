//
// Created by orisa on 28/04/2025.
//
#define MAIN_THREAD_TID 0
#define STACK_SIZE 100000 /* stack size per thread (in bytes) */
#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
  address_t ret;
  asm volatile("xor    %%fs:0x30,%0\n"
               "rol    $0x11,%0\n"
      : "=g" (ret)
      : "0" (addr));
  return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5


/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
                 "rol    $0x9,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}


#endif
#include <setjmp.h>
#include <signal.h>
#include "thread.h"


typedef unsigned long address_t;
typedef void (*thread_entry_point)(void);
typedef void (*callback_t)(int);

Thread::Thread(int tid, State state, bool is_main, thread_entry_point
    entry_point, char* stack, callback_t terminate_callback) : tid
                                          (tid), state(state), entry_point(entry_point), stack(stack),
                                      is_sleeping(false), terminate_callback(terminate_callback)
    {
      quantums_passed = is_main ? 1 : 0;
      setup_env();
    }

int Thread::get_tid () const
  {
    return tid;
  }

  void Thread::set_state (State state)
  {
    this->state = state;
  }
  void Thread::increment_quantum ()
  {
    this->quantums_passed++;
  }
  // Getter for the jmp_buf (so you can access it in the handler)
  jmp_buf* Thread::get_env() {
    return &env;
  }

  State Thread::get_state () const
  {
    return state;
  }
  int Thread::get_quantums_passed () const
  {
    return quantums_passed;
  }
  int Thread::get_remaining_sleep_time () const
  {
    return remaining_sleep_time;
  }
  void Thread::set_remaining_sleep_time (int time_left)
  {
      remaining_sleep_time = time_left;
  }
  bool Thread::get_is_sleeping () const
  {
    return is_sleeping;
  }
  void Thread::set_is_sleeping (bool is_sleeping)
  {
    Thread::is_sleeping = is_sleeping;
  }

  // Initialize the execution context for the thread
  // Modify your setup_env method
  bool Thread::setup_env() {
    if (sigsetjmp(env, 1) == 0) {
      // If this is the main thread or we're just saving context, return true
      if (tid == MAIN_THREAD_TID || !entry_point) {
        return true;
      }

      // Setup for a new thread with entry_point
      // Calculate the top of the stack (stack grows downward)
      address_t sp = (address_t)stack + STACK_SIZE - sizeof(address_t);

      // Set the program counter to the entry point function
      address_t pc = (address_t)entry_point;

      (void)sigsetjmp(env, 1);
      // Translate and store both addresses in the jmp_buf
      (env->__jmpbuf)[JB_SP] = translate_address(sp);
      (env->__jmpbuf)[JB_PC] = translate_address(pc);
      (void)sigemptyset(&env->__saved_mask);
      return true;
    }

    // If we get here, we've returned from siglongjmp
    // This is where the new thread actually starts executing
    // For non-main threads, call their entry point
    if (tid != MAIN_THREAD_TID && entry_point) {
      entry_point();

      // If the entry point returns, we need to terminate the thread
      terminate_callback(tid);
    }

    return false;
  }

  // Destructor to free the stack memory
  Thread::~Thread() {
    if (stack != nullptr) {
      delete[] stack;
      stack = nullptr;
    }
  }
