#ifndef THREAD_H
#define THREAD_H

#include <setjmp.h>
#include <signal.h>

#define MAIN_THREAD_TID 0
#define STACK_SIZE 100000 /* stack size per thread (in bytes) */
typedef void (*callback_t)(int);
// Architecture-specific definitions
typedef unsigned long address_t;
address_t translate_address(address_t addr);
// Enum for thread state
enum State {
    RUNNING, BLOCKED, READY
};

// Type for thread entry point
typedef void (*thread_entry_point)(void);

class Thread {
 private:
  int tid;  // Thread ID
  State state;  // Thread state
  int quantums_passed;  // Quantum count for the thread
  jmp_buf env;  // Execution context
  thread_entry_point entry_point;  // Entry point for the thread
  char* stack;  // Pointer to the thread's stack
  int remaining_sleep_time;  // Time left for sleeping thread
  bool is_sleeping;  // Sleep status of the thread
  callback_t terminate_callback;

 public:
  // Constructor
  Thread(int tid, State state, bool is_main, thread_entry_point entry_point
  = nullptr, char* stack = nullptr, callback_t terminate_callback= nullptr);

  // Getters and setters
  int get_tid() const;
  void set_state(State state);
  void increment_quantum();
  jmp_buf* get_env();
  State get_state() const;
  int get_quantums_passed() const;
  int get_remaining_sleep_time() const;
  void set_remaining_sleep_time(int time_left);
  bool get_is_sleeping() const;
  void set_is_sleeping(bool is_sleeping);

  // Method to set up the execution environment
  bool setup_env();

  // Destructor to free the stack memory
  ~Thread();
};

#endif  // THREAD_H
