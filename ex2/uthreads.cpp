#include "uthreads.h"
#include <iostream>
#include <unordered_map>
#include <queue>
#include <vector>
#include <functional>
#include <csignal>      // for sigaction, SIGVTALRM
#include <sys/time.h>   // for setitimer
#include <iostream>
#include <cstring>
#include <cerrno>
#include <setjmp.h>


using std::queue;
using std::unordered_map;
using std::vector;
using std::priority_queue;
using std::greater;

#define MAIN_THREAD_TID 0
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

// ========================
// Internal Global Variables
// ========================
class context_switch_lock
{
 public:
  context_switch_lock() { disable_context_switch (); }
  ~context_switch_lock() { enable_context_switch (); }

 private:
  static void disable_context_switch()
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

  static void enable_context_switch()
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
};

enum State{
    RUNNING, BLOCKED, READY
};

class Thread{

  private:
    int tid;
    State state;
    int quantums_passed;
    jmp_buf env; // store the execution context
    thread_entry_point entry_point;
    void* stack;  // Pointer to the thread's stack
    int remaining_sleep_time;
    bool is_sleeping;



 public:
    Thread(int tid, State state, bool is_main, thread_entry_point
    entry_point = nullptr, void* stack= nullptr) : tid(tid), state(state),
    entry_point
    (entry_point), stack(stack), is_sleeping(false){
      quantums_passed = is_main ? 1 : 0;
      setup_env();
    }

  void set_state (State state)
  {
    this->state = state;
  }
  void increment_quantum ()
  {
    this->quantums_passed++;
  }
  // Getter for the jmp_buf (so you can access it in the handler)
  jmp_buf* get_env() {
    return &env;
  }

  State get_state () const
  {
    return state;
  }
  int get_quantums_passed () const
  {
    return quantums_passed;
  }
  int get_remaining_sleep_time () const
  {
    return remaining_sleep_time;
  }
  void set_remaining_sleep_time (int time_left)
  {
    Thread::remaining_sleep_time = time_left;
  }
  bool get_is_sleeping () const
  {
    return is_sleeping;
  }
  void set_is_sleeping (bool is_sleeping)
  {
    Thread::is_sleeping = is_sleeping;
  }

  // Initialize the execution context for the thread
  // Modify your setup_env method
  bool setup_env() {
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

      // Translate and store both addresses in the jmp_buf
      (env->__jmpbuf)[JB_SP] = translate_address(sp);
      (env->__jmpbuf)[JB_PC] = translate_address(pc);

      return true;
    }

    // If we get here, we've returned from siglongjmp
    // This is where the new thread actually starts executing
    // For non-main threads, call their entry point
    if (tid != MAIN_THREAD_TID && entry_point) {
      entry_point();

      // If the entry point returns, we need to terminate the thread
      uthread_terminate(tid);
    }

    return false;
  }

  // Destructor to free the stack memory
  ~Thread() {
    free(stack);
  }
};

void remove_tid_from_ready_queue (int tid);
void terminate_process ();
void schedule ();
static int quantum_def;
static int quantums_passed;
static int current_running_thread_tid;

static queue<int> ready_queue;
static unordered_map<int,Thread*> threads_map;
//static unordered_map<int, int> sleeping_threads;
static priority_queue<int, vector<int>, greater<int>> available_tids;
static vector<int> threads_to_delete;
static int thread_count;
// ========================
// Function Definitions
// ========================

void init_available_tids() {
  for (int i = 1; i < MAX_THREAD_NUM; ++i) {
    available_tids.push(i);
  }
}
void switch_threads(bool is_blocked = false, bool terminate_running = false);

void delete_awaiting_threads ();
void update_sleeping_threads(){
  for (int i = 0; i < MAX_THREAD_NUM; ++i)
  {
    if(threads_map[i] == nullptr){
      continue;
    }
    int remaining_sleep_time = threads_map[i]->get_remaining_sleep_time ();
    if(remaining_sleep_time != 0){
      threads_map[i]->set_remaining_sleep_time (remaining_sleep_time - 1);

      // if the thread is done sleeping and not blocked,
      // we wake it up and add it to the ready queue
      if(threads_map[i]->get_remaining_sleep_time() == 0 &&
                                                         threads_map[i]->get_is_sleeping ()){
        ready_queue.push (i);
        threads_map[i]->set_state (READY);
        threads_map[i]->set_is_sleeping(false);
      }
    }
  }
}
// This function will be called every time the quantum ends
void timer_handler(int sig) {

  // needs to do 2 things:
  // 1) move sleeper threads into the ready queue if finished sleep
  delete_awaiting_threads();
  update_sleeping_threads();
  // 2) switch to the next thread TODO WHY?
  switch_threads (false, false);



//  (void)sig; // to suppress unused warning

//  schedule();
//  switch_threads(false, false);
//  if (sigsetjmp(*threads_map[current_running_thread_tid]->get_env (), 1) !=
//  0) {
//    return; // if returning from siglongjmp, just return
//  }
//  //  1. If it was preempted because its quantum has expired, move it to the end of the
//  //  READY threads list.
//  threads_map[current_running_thread_tid]->set_state (READY);
//  ready_queue.push (current_running_thread_tid);
//  //  2. Move the next thread in the queue of READY threads to the RUNNING state
//  current_running_thread_tid = ready_queue.front();
//  ready_queue.pop();
//
//  threads_map[current_running_thread_tid]->set_state(RUNNING);
//  threads_map[current_running_thread_tid]->increment_quantum();
//
//  quantums_passed++;
//
//  siglongjmp(*threads_map[current_running_thread_tid]->get_env (), 1);

}

// Sets up the signal handler for SIGVTALRM
void setup_signal_handler() {
  struct sigaction sa{};
  sa.sa_handler = &timer_handler;
  int sigset_retval = 0;
  sigset_retval += sigemptyset(&sa.sa_mask);
  sigset_retval += sigaddset(&sa.sa_mask, SIGVTALRM);
  sigset_retval += sigaction(SIGVTALRM, &sa, nullptr);

  if (sigset_retval != 0) {
    std::cerr << "system error: Error setting up signal handler" << std::endl;
    exit(1);
  }
}


// Sets up a virtual timer to fire every quantum_usecs microseconds
void setup_timer() {
  struct itimerval timer{};

  timer.it_value.tv_sec = quantum_def / 1000000;
  timer.it_value.tv_usec = quantum_def % 1000000;

  timer.it_interval.tv_sec = quantum_def / 1000000;
  timer.it_interval.tv_usec = quantum_def % 1000000;

  if (setitimer(ITIMER_VIRTUAL, &timer, nullptr) == -1) {
    std::cerr << "system error: setitimer failed: " << strerror(errno) << std::endl;
    exit(1);
  }
}

/**
 * @brief initializes the thread library.
 *
 * Once this function returns, the main thread (tid == 0) will be set as RUNNING. There is no need to
 * provide an entry_point or to create a stack for the main thread - it will be using the "regular" stack and PC.
 * You may assume that this function is called before any other thread library function, and that it is called
 * exactly once.
 * The input to the function is the length of a quantum in micro-seconds.
 * It is an error to call this function with non-positive quantum_usecs.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_init(int quantum_usecs)
{
  if(quantum_usecs <= 0){
    std::cerr << "thread library error: quantum should be positive\n";
    return -1;
  }
  current_running_thread_tid = MAIN_THREAD_TID;
  quantum_def = quantum_usecs;
  quantums_passed = 1;
  Thread* main_thread;
try{
  main_thread = new Thread(MAIN_THREAD_TID,RUNNING, true, nullptr, nullptr);
} catch (const std::bad_alloc&){
  std::cerr << "system error: main thread allocation failed\n";
}
  threads_map[MAIN_THREAD_TID] = main_thread;

  thread_count = 1;
  setup_signal_handler();
  setup_timer ();

  init_available_tids();

  return 0;
}


/**
 * @brief Creates a new thread, whose entry point is the function entry_point with the signature
 * void entry_point(void).
 *
 * The thread is added to the end of the READY threads list.
 * The uthread_spawn function should fail if it would cause the number of concurrent threads to exceed the
 * limit (MAX_THREAD_NUM).
 * Each thread should be allocated with a stack of size STACK_SIZE bytes.
 * It is an error to call this function with a null entry_point.
 *
 * @return On success, return the ID of the created thread. On failure, return -1.
 */
int uthread_spawn(thread_entry_point entry_point)
{
  context_switch_lock lock;
  delete_awaiting_threads();
  if(thread_count == MAX_THREAD_NUM){
    std::cerr << "thread library error: cannot create new thread, max "
                 "threads reached.\n";
    return -1;
  }
  if(entry_point == nullptr){
    std::cerr << "thread library error: Thread entry point can't be null\n";
    return -1;
  }

  if (available_tids.empty()) {
    std::cerr << "thread library error: no available thread IDs\n";
    return -1;
  }

  // Allocate memory for the thread's stack
  void* stack = malloc(STACK_SIZE);
  if (!stack) {
    std::cerr << "thread library error: failed to allocate memory for thread stack\n";
    return -1;
  }

  int new_tid = available_tids.top();
  available_tids.pop();


  Thread* new_thread = new Thread(new_tid, READY, false, entry_point, stack);
  threads_map[new_tid] = new_thread;

  ready_queue.push (new_tid);

  thread_count++;


  return new_tid;
}
static void destroy_thread(int tid){
//  std::cerr << "Destroying thread: " << tid << std::endl;
  Thread* thread = threads_map[tid];
  delete thread;
  threads_map[tid] = nullptr;
//  sleeping_threads.erase(tid);
  available_tids.push(tid);
  thread_count--;
}

void delete_awaiting_threads ()
{
  for(const int tid : threads_to_delete){
    destroy_thread(tid);
  }
  threads_to_delete.clear();
}

bool thread_exists(int tid){
  return threads_map.find (tid) != threads_map.end();
}


/**
 * @brief Terminates the thread with ID tid and deletes it from all relevant control structures.
 *
 * All the resources allocated by the library for this thread should be released. If no thread with ID tid exists it
 * is considered an error. Terminating the main thread (tid == 0) will result in the termination of the entire
 * process using exit(0) (after releasing the assigned library memory).
 *
 * @return The function returns 0 if the thread was successfully terminated and -1 otherwise. If a thread terminates
 * itself or the main thread is terminated, the function does not return.
 */
int uthread_terminate(int tid)
{
    context_switch_lock lock;
    if(tid == 0){
      terminate_process();
    }
    if(!thread_exists(tid))
    {
      std::cerr << "thread library error: no thread with tid: " << tid << "\n";
      return -1;
    }
    Thread* thread_to_terminate = threads_map[tid];
    if(thread_to_terminate -> get_state() == RUNNING){
//      switch_threads (true, true);
      threads_to_delete.push_back(tid);  // Defer deletion!
      remove_tid_from_ready_queue (tid);
      switch_threads(true, false);       // Just mark it blocked, do NOT delete here
      return 0;
    }
    else if (thread_to_terminate -> get_state() == READY){
      remove_tid_from_ready_queue (tid);
    }


    // Add tid back to available_tids for reuse
    destroy_thread(tid);
    return 0;
  }
void terminate_process ()
{
  for (auto& pair : threads_map) {
    int tid = pair.first;
    Thread* second = pair.second;
    // Skip terminating the main thread (tid == 0) if that's part of your design
    delete second;
  }
  exit(0);
}
void remove_tid_from_ready_queue (int tid)
{
  std::queue<int> temp_queue;

  // Iterate through the ready queue to find and remove the tid
  while (!ready_queue.empty()) {
    int current_tid = ready_queue.front();
    ready_queue.pop();
    if (current_tid != tid) {
      temp_queue.push(current_tid);  // Keep the other TIDs
    }
  }

  // Update the original ready queue to reflect the removal
  ready_queue = temp_queue;
}


/**
 * @brief Blocks the thread with ID tid. The thread may be resumed later using uthread_resume.
 *
 * If no thread with ID tid exists it is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision should be made. Blocking a thread in
 * BLOCKED state has no effect and is not considered an error.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_block(int tid)
{
  context_switch_lock lock;
  if(tid == MAIN_THREAD_TID){
    std::cerr << "thread library error: can't block main thread: \n";
    return -1;
  }
  if(!thread_exists(tid)){
    std::cerr << "thread library error: no thread with tid: " << tid << "\n";
    return -1;
  }
  if (tid == current_running_thread_tid)
  {
    switch_threads(true, false);
  }
  // if we got here there is such a thread
  Thread* thread_to_block = threads_map[tid];
  if(thread_to_block == nullptr){
    std::cerr << "thread library error: blocking funciton, no such thread "
                 "with tid "<< tid << std::endl;
    return -1;
  }
  if(thread_to_block->get_state() == BLOCKED){
    return 0;
  }
  remove_tid_from_ready_queue (tid);
//  if(tid == current_running_thread_tid){
//    // If the current running thread is being blocked, we need to perform a scheduling decision
//    // 1. Move the current thread to the BLOCKED state
//    thread_to_block->set_state(BLOCKED);
//
//    // 2. Check the ready queue isn't empty:
//    if (ready_queue.empty()) {
//      std::cerr << "thread library error: no threads to schedule after blocking the current one.\n";
//      return -1;
//    }
//
//    // 3. Select the next thread to run
//    current_running_thread_tid = ready_queue.front();
//    ready_queue.pop();
//
//    // 4. Set the new running thread state to RUNNING
//    threads_map[current_running_thread_tid]->set_state(RUNNING);
//
//    // Switch context: saving current thread's context and jumping to the new thread
//    siglongjmp(*threads_map[current_running_thread_tid]->get_env(), 1);
//  }
  thread_to_block->set_state (BLOCKED);
  return 0;
}


/**
 * @brief Resumes a blocked thread with ID tid and moves it to the READY state.
 *
 * Resuming a thread in a RUNNING or READY state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_resume(int tid)
{
  context_switch_lock lock;
  if(!thread_exists(tid)){
    std::cerr << "thread library error: no thread with tid: " << tid << "\n";
    return -1;
  }
  // if we got here there is such a thread
  Thread* thread_to_resume = threads_map[tid];
  State thread_state = thread_to_resume -> get_state();
  if(thread_state == RUNNING || thread_state == READY){
    return 0;
  }
  if(thread_state == BLOCKED && thread_to_resume->get_remaining_sleep_time()
  == 0){
    thread_to_resume->set_state (READY);
    ready_queue.push (tid);
  }
  return 0;
}


/**
 * @brief Blocks the RUNNING thread for num_quantums quantums.
 *
 * Immediately after the RUNNING thread transitions to the BLOCKED state a scheduling decision should be made.
 * After the sleeping time is over, the thread should go back to the end of the READY queue.
 * If the thread which was just RUNNING should also be added to the READY queue, or if multiple threads wake up
 * at the same time, the order in which they're added to the end of the READY queue doesn't matter.
 * The number of quantums refers to the number of times a new quantum starts, regardless of the reason. Specifically,
 * the quantum of the thread which has made the call to uthread_sleep isn’t counted.
 * It is considered an error if the main thread (tid == 0) calls this function.
 *
 * @return On success, return 0. On failure, return -1.
 */
int uthread_sleep(int num_quantums)
{
  context_switch_lock lock;
  if (current_running_thread_tid == MAIN_THREAD_TID) {
    std::cerr << "thread library error: main thread cannot sleep.\n";
    return -1;
  }

  if (num_quantums <= 0) {
    std::cerr << "thread library error: invalid number of quantums.\n";
    return -1;
  }
  // Get the current running thread
  Thread* running_thread = threads_map[current_running_thread_tid];

  // Move the current thread to BLOCKED state
//  running_thread->set_state(BLOCKED); // todo check if in switch threads

  // Add the sleeping thread to a map with num_quantums as the key (to track when it should be resumed)
//  sleeping_threads[current_running_thread_tid] = num_quantums +
//      quantums_passed;

  running_thread->set_remaining_sleep_time (num_quantums);
  running_thread->set_is_sleeping (true);
  switch_threads(false, false);

  return 0;
}
//void schedule ()
//{
////  context_switch_lock lock;
////  // Check sleeping threads and move them back to READY if their time is up
////  for (auto it = sleeping_threads.begin(); it != sleeping_threads.end(); ) {
////    int tid = it->first;
////    int remaining_quantums = it->second;
////
////    if (remaining_quantums <= 0) {
////      // The thread has finished sleeping
////      Thread* thread_to_wake = threads_map[tid];
////      thread_to_wake->set_state(READY);
////      ready_queue.push(tid);
////
////      // Remove from sleeping threads
////      it = sleeping_threads.erase(it);  // Erase and move to next element
////    } else {
////      // Decrease the number of quantums remaining
////      it->second--;
////      ++it;
////    }
////  }
//  context_switch_lock lock;
//
//  // Update sleep time for all sleeping threads
//  for (auto& thread_pair : threads_map) {
//    auto* thread = thread_pair.second;
//    if (thread->get_state() == SLEEPING && thread->get_remaining_sleep_time () > 0) {
//      // Decrement the remaining sleep time by the timer's period (e.g., 1 ms)
//      thread->set_remaining_sleep_time (
//          thread->get_remaining_sleep_time () - quantum_def);
//
//      // If the thread's sleep time has expired, move it to the READY state
//      if (thread->get_remaining_sleep_time () <= 0) {
//        thread->set_state(READY);
//        ready_queue.push(thread_pair.first);  // Add to ready queue
//      }
//    }
//  }
//
//  // Check if there's a thread in the ready queue to run
//  if (ready_queue.empty() && thread_count > 1) {
//    // If no thread is ready, but we have threads, we should call for a timer to yield control
//    return;
//  }
//
//  // Choose the next thread to run (e.g., choose the next ready thread)
//  int next_tid = ready_queue.front();  // Or any other logic to pick the next thread
//
//  // Ensure that the current thread is not selected again
//  while (next_tid == current_running_thread_tid && !ready_queue.empty()) {
//    ready_queue.pop();
//    next_tid = ready_queue.front();  // Pick the next available thread
//  }
//
//  // If we have a valid next thread, do a context switch
//  if (next_tid != current_running_thread_tid && next_tid != 0) {
//    // Mark the current thread as ready again if it's not terminated
//    if (threads_map[current_running_thread_tid] != nullptr) {
//      threads_map[current_running_thread_tid]->set_state(READY);
//      ready_queue.push(current_running_thread_tid);
//    }
//
//    // Set the next thread to running
//    current_running_thread_tid = next_tid;
//    threads_map[current_running_thread_tid]->set_state(RUNNING);
//
//    // Perform the context switch (the actual switch happens here)
//    switch_threads(false, false);
//  }
//}

/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
 */
int uthread_get_tid()
{
  context_switch_lock lock;
  return current_running_thread_tid;
}


/**
 * @brief Returns the total number of quantums since the library was initialized, including the current quantum.
 *
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number should be increased by 1.
 *
 * @return The total number of quantums.
 */
int uthread_get_total_quantums()
{
  context_switch_lock lock;
  return quantums_passed;
}


/**
 * @brief Returns the number of quantums the thread with ID tid was in RUNNING state.
 *
 * On the first time a thread runs, the function should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state when this function is called, include
 * also the current quantum). If no thread with ID tid exists it is considered an error.
 *
 * @return On success, return the number of quantums of the thread with ID tid. On failure, return -1.
 */
int uthread_get_quantums(int tid)
{
  context_switch_lock lock;
  if(!thread_exists(tid)){
    std::cerr << "thread library error: no thread with tid: " << tid << "\n";
    return -1;
  }
  return threads_map[tid]->get_quantums_passed();
}

void switch_threads(bool is_blocked, bool terminate_running)
{
  int old_tid = current_running_thread_tid;
  Thread* current_thread = threads_map[old_tid];

  // Save the current context
  if (!terminate_running && sigsetjmp(*current_thread->get_env(), 1) != 0)
  {
    return;  // We're returning from a context switch
  }

  if (!is_blocked && !terminate_running)
  {
    current_thread->set_state (READY);
    ready_queue.push (current_running_thread_tid);
  }
  else if(!terminate_running){
    setup_timer();
    current_thread->set_state (BLOCKED);
  }
  // Update state of current thread
  if (terminate_running)
  {
//    threads_to_delete.push_back (old_tid);
    destroy_thread (old_tid);
  }


  // Pick next thread to run
  if (ready_queue.empty())
  {
    std::cerr << "thread library error: no READY threads to run.\n";
    exit(1);  // Or some graceful shutdown
  }

  current_running_thread_tid = ready_queue.front();
  ready_queue.pop();

  Thread* next_thread = threads_map[current_running_thread_tid];
  next_thread->set_state(RUNNING);
  next_thread->increment_quantum();
  quantums_passed++;
//  std::cerr << "Switching to thread: " << current_running_thread_tid << std::endl;

  siglongjmp(*next_thread->get_env(), 1);
}

