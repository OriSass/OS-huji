#include "resources/uthreads.h"
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

// ========================
// Internal Global Variables
// ========================

// TODO: Add thread management structures here (e.g., ready queue, threads map, etc.)

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



 public:
    Thread(int tid, State state, bool is_main, thread_entry_point
    entry_point = nullptr, void* stack= nullptr) : tid(tid), state(state),
    entry_point
    (entry_point), stack(stack){
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

  // Initialize the execution context for the thread
  bool setup_env() {
    if (sigsetjmp(env, 1) == 0) {
      return true;  // If sigsetjmp returns 0, it is called the first time
    }
    return false; // If sigsetjmp returns non-zero, it was called via siglongjmp
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
static unordered_map<int, int> sleeping_threads;
static priority_queue<int, vector<int>, greater<int>> available_tids;
static int thread_count;
// ========================
// Function Definitions
// ========================

void init_available_tids() {
  for (int i = 1; i < MAX_THREAD_NUM; ++i) {
    available_tids.push(i);
  }
}

// This function will be called every time the quantum ends
void timer_handler(int sig) {
  std::cout << "Quantum expired! Performing context switch..." << std::endl;
  (void)sig; // to suppress unused warning

  schedule();

  if (sigsetjmp(reinterpret_cast<__jmp_buf_tag *>(threads_map[current_running_thread_tid]->get_env ()), 1) != 0) {
    return; // if returning from siglongjmp, just return
  }
  //  1. If it was preempted because its quantum has expired, move it to the end of the
  //  READY threads list.
  threads_map[current_running_thread_tid]->set_state (READY);
  ready_queue.push (current_running_thread_tid);
  //  2. Move the next thread in the queue of READY threads to the RUNNING state
  current_running_thread_tid = ready_queue.front();
  ready_queue.pop();

  threads_map[current_running_thread_tid]->set_state(RUNNING);
  threads_map[current_running_thread_tid]->increment_quantum();

  quantums_passed++;

  siglongjmp(reinterpret_cast<__jmp_buf_tag *>(threads_map[current_running_thread_tid]->get_env ()), 1);

}

// Sets up the signal handler for SIGVTALRM
void setup_signal_handler() {
  struct sigaction sa{};
  sa.sa_handler = &timer_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  if (sigaction(SIGVTALRM, &sa, nullptr) < 0) {
    std::cerr << "Error setting up signal handler" << std::endl;
    exit(1);
  }
}


// Sets up a virtual timer to fire every quantum_usecs microseconds
void setup_timer(int quantum_usecs) {
  struct itimerval timer{};

  timer.it_value.tv_sec = quantum_usecs / 1000000;
  timer.it_value.tv_usec = quantum_usecs % 1000000;

  timer.it_interval.tv_sec = quantum_usecs / 1000000;
  timer.it_interval.tv_usec = quantum_usecs % 1000000;

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

  Thread* main_thread = new Thread(MAIN_THREAD_TID,RUNNING, true, nullptr, nullptr);
  threads_map[MAIN_THREAD_TID] = main_thread;

  thread_count = 1;
  setup_signal_handler();
  setup_timer (quantum_def);

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
    if(tid == 0){
      terminate_process();
      exit(0);
    }
    if(!thread_exists(tid))
    {
      std::cerr << "thread library error: no thread with tid: " << tid << "\n";
      return -1;
    }
    // if we got here we are in the main case where there is such a thread and
    // we need to delete it
    Thread* thread_to_terminate = threads_map[tid];


    remove_tid_from_ready_queue(tid);
    threads_map.erase(tid);
    // Add tid back to available_tids for reuse
    available_tids.push(tid);

    delete thread_to_terminate;
    thread_count--;

    return 0;
  }
void terminate_process ()
{
  for (auto& pair : threads_map) {
    int tid = pair.first;
    // Skip terminating the main thread (tid == 0) if that's part of your design
    if (tid != MAIN_THREAD_TID) {
      uthread_terminate(tid);
    }
  }
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
  if(tid == MAIN_THREAD_TID){
    std::cerr << "thread library error: can't block main thread: \n";
    return -1;
  }
  if(!thread_exists(tid)){
    std::cerr << "thread library error: no thread with tid: " << tid << "\n";
    return -1;
  }
  // if we got here there is such a thread
  Thread* thread_to_block = threads_map[tid];
  if(thread_to_block->get_state() == BLOCKED){
    return 0;
  }
  if(tid == current_running_thread_tid){
    // If the current running thread is being blocked, we need to perform a scheduling decision
    // 1. Move the current thread to the BLOCKED state
    thread_to_block->set_state(BLOCKED);

    // 2. Make the scheduling decision:
    // Move the blocked thread to the end of the ready queue
    ready_queue.push(current_running_thread_tid);

    // 3. Select the next thread to run
    current_running_thread_tid = ready_queue.front();
    ready_queue.pop();

    // 4. Set the new running thread state to RUNNING
    threads_map[current_running_thread_tid]->set_state(RUNNING);

    // Switch context: saving current thread's context and jumping to the new thread
    siglongjmp(*threads_map[current_running_thread_tid]->get_env(), 1);
  }
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
  if(thread_state == BLOCKED){
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
  running_thread->set_state(BLOCKED);

  // Add the sleeping thread to a map with num_quantums as the key (to track when it should be resumed)
  sleeping_threads[current_running_thread_tid] = num_quantums;

  schedule();

  return 0;
}
void schedule ()
{
  // Check sleeping threads and move them back to READY if their time is up
  for (auto it = sleeping_threads.begin(); it != sleeping_threads.end(); ) {
    int tid = it->first;
    int remaining_quantums = it->second;

    if (remaining_quantums <= 0) {
      // The thread has finished sleeping
      Thread* thread_to_wake = threads_map[tid];
      thread_to_wake->set_state(READY);
      ready_queue.push(tid);

      // Remove from sleeping threads
      it = sleeping_threads.erase(it);  // Erase and move to next element
    } else {
      // Decrease the number of quantums remaining
      it->second--;
      ++it;
    }
  }
}

/**
 * @brief Returns the thread ID of the calling thread.
 *
 * @return The ID of the calling thread.
 */
int uthread_get_tid()
{
  // TODO: implement
  return 0;
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
  // TODO: implement
  return 0;
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
  // TODO: implement
  return 0;
}
