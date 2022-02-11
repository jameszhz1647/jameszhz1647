#include <stdio.h>

#include "debug.h"

#include "context.h"
#include "scheduler.h"
#include "jobqueue.h"
#include "job.h"
#include "event.h"

// Enable debugging for this scheduler? 1=True
#define DEBUG_SCHED_LOTTERY_ALL 1

#if DEBUG_SCHED_LOTTERY_ALL
#define DEBUG(fmt, args...) DEBUG_PRINT("lottery_all: " fmt, ##args)
#else
#define DEBUG(fmt, args...)
#endif
#define ERROR(fmt, args...) ERROR_PRINT("lottery_all: " fmt, ##args)
#define INFO(fmt, args...)  INFO_PRINT("lottery_all: " fmt, ##args)


// Struct definition that holds state for this scheduler
typedef struct lottery_all_state {
  sim_sched_t* sim;
} lottery_all_state_t;

// State for this scheduler
lottery_all_state_t my_state;
sim_job_t* current_job = 0;
sim_event_t* current_event = 0;
double current_time = 0.0;
static float priority_cnt = 0;

// Initialization for this scheduler
static int init(void* state, sim_context_t* context) {
  sim_event_t* interrupt_e  = 0;
  interrupt_e = sim_event_create(sim_context_get_cur_time(context) + 0.01,
                       context,
                       SIM_EVENT_TIMER,
                       1);

  if (!interrupt_e) {
    ERROR("failed to allocate event\n");
    return;
  }
  sim_event_queue_post(&context->event_queue, interrupt_e);
  priority_cnt = 0;
  current_time = sim_context_get_cur_time(context);
  DEBUG("start");
  return 0;
}


static int func(void* state, sim_job_t* job)
{
  double* jobs = (double *) state;
  // DEBUG("func\n");
  if (job->size > *jobs)
  {
    // DEBUG("equal\n");
    return 1;
  }
  else
  {
    // DEBUG("not\n");
    return 0;
  } 


}


static int count = 0;
// for random 
static int random_func(void* state, sim_job_t* job)
{

  int* rand_job = (int *) state;

  if (*rand_job == count){
    count = 0;
    return 1;
  }
  else
  {
    count ++;
    return 0;
  }

}

// Function called when an aperiodic job arrives
static sim_sched_acceptance_t aperiodic_job_arrival(void* state,
                                                    sim_context_t* context,
                                                    sim_job_t* job) {

  // fifo_all_state_t* s = (fifo_all_state_t*)state;

  DEBUG("%lf arrival job %lu size %lf\n", sim_context_get_cur_time(context), job->id, job->size);

  sim_job_queue_enqueue(&context->aperiodic_queue, job);
  priority_cnt = priority_cnt + job->static_priority;


  if(current_job!=NULL){
    current_job->remaining_size = current_job->remaining_size + current_time - sim_context_get_cur_time(context);

    int rand_num = 0;
    struct list_head* cur, * temp;
    list_for_each_safe(cur, temp, &context->aperiodic_queue.list) {
        sim_job_t* j = list_entry(cur, sim_job_t, node);
        float p=(float)j->static_priority/priority_cnt;
        float tmp_p = (float)rand()/RAND_MAX;
        if(tmp_p <= p){
            break;
        }
        rand_num = rand_num + 1;
    }
    if (context->aperiodic_queue.num_jobs > 0 && rand_num == context->aperiodic_queue.num_jobs){
      rand_num = rand_num - 1;
    }

    sim_event_queue_delete(&context->aperiodic_queue, current_event);

    sim_job_t* next = 0;
    sim_event_t* e  = 0;

    DEBUG("rand_num %u\n", rand_num);
    next = sim_job_queue_search( &context->aperiodic_queue, &random_func, (void *) &rand_num);
    if (!next) {
        DEBUG("no more jobs in queue\n");
        current_job = 0;
        return;
    }
    e = sim_event_create(sim_context_get_cur_time(context) + next->remaining_size,
                           context,
                           SIM_EVENT_JOB_DONE,
                           next); 
    if (!e) {
        ERROR("failed to allocate event\n");
        return;
      }   
    DEBUG("%lf switching to job %lu size %lf remaining size %lf\n", sim_context_get_cur_time(context), next->id, next->size, next->remaining_size);

    sim_event_queue_post(&context->event_queue, e);
    current_job = next;
    current_event = e;
    current_time = sim_context_get_cur_time(context); 
  }else{
    DEBUG("starting new job %lu because we are idle\n", job->id);

    sim_event_t* e = sim_event_create(sim_context_get_cur_time(context) + job->remaining_size,
                                      context,
                                      SIM_EVENT_JOB_DONE,
                                      job);

    if (!e) {
      ERROR("failed to allocate event\n");
      return SIM_SCHED_REJECT;
    }

    sim_event_queue_post(&context->event_queue, e);

    current_job = job;
    current_event = e;
    current_time = sim_context_get_cur_time(context);
    DEBUG("cur_event2 %lu\n", current_event->id);
  }

  return SIM_SCHED_ACCEPT;
}

// Function called when a job is finished
static void job_done(void* state,
                     sim_context_t* context,
                     sim_job_t* job) {

  // fifo_all_state_t* s = (fifo_all_state_t*)state;

  DEBUG("%lf done job %lu size %lf remaining size %lf\n", sim_context_get_cur_time(context), job->id, job->size, job->remaining_size);

  priority_cnt = priority_cnt - job->static_priority;
  // dequeue this current job
  sim_job_t* next = 0;
  sim_event_t* e  = 0;

  sim_job_queue_remove(&context->aperiodic_queue, job);

  if (sim_job_complete(context, job)) {
    ERROR("failed to complete job\n");
    return;
  }

  int rand_num = 0;
  struct list_head* cur, * temp;
  list_for_each_safe(cur, temp, &context->aperiodic_queue.list) {
        sim_job_t* j = list_entry(cur, sim_job_t, node);
        float p=(float)j->static_priority/priority_cnt;
        float tmp_p = (float)rand()/RAND_MAX;
        if(tmp_p <= p){
            break;
        }
        rand_num = rand_num + 1;
  }
  if (context->aperiodic_queue.num_jobs > 0 && rand_num == context->aperiodic_queue.num_jobs){
      rand_num = rand_num - 1;
  }


  DEBUG("rand_num %u\n", rand_num);
  next = sim_job_queue_search( &context->aperiodic_queue, &random_func, (void *) &rand_num);
  
  // next = sim_job_queue_peek(&context->aperiodic_queue);

  if (!next) {
    DEBUG("no more jobs in queue\n");
    return;
  }

  e = sim_event_create(sim_context_get_cur_time(context) + next->remaining_size,
                       context,
                       SIM_EVENT_JOB_DONE,
                       next);

  if (!e) {
    ERROR("failed to allocate event\n");
    return;
  }

  DEBUG("%lf switching to job %lu size %lf remaining size %lf\n", sim_context_get_cur_time(context), next->id, next->size, next->remaining_size);

  sim_event_queue_post(&context->event_queue, e);
  current_job = next;
  current_event = e;
  current_time = sim_context_get_cur_time(context);
}

// Function called when a job is blocked
static void job_blocked(void* state,
                        sim_context_t* context,
                        sim_job_t* job) {
  // fifo_all_state_t* s = (fifo_all_state_t*)state;
  DEBUG("ignoring job blocked\n");
}

// Function called when a timeslice expires
static void timer_interrupt(void* state,
                            sim_context_t* context) {
  // fifo_all_state_t* s = (fifo_all_state_t*)state;
  sim_event_t* interrupt_e  = 0;
  interrupt_e = sim_event_create(sim_context_get_cur_time(context) + 0.01,
                       context,
                       SIM_EVENT_TIMER,
                       1);

  if (!interrupt_e) {
    ERROR("failed to allocate event\n");
    return;
  }
  sim_event_queue_post(&context->event_queue, interrupt_e);

  if(current_job){
    current_job->remaining_size = current_job->remaining_size + current_time - sim_context_get_cur_time(context);
    sim_event_queue_delete(&context->aperiodic_queue, current_event);

    sim_job_t* next = 0;
    sim_event_t* e  = 0;

    int rand_num = 0;
    struct list_head* cur, * temp;
    list_for_each_safe(cur, temp, &context->aperiodic_queue.list) {
        sim_job_t* j = list_entry(cur, sim_job_t, node);
        float p=(float)j->static_priority/priority_cnt;
        float tmp_p = (float)rand()/RAND_MAX;
        if(tmp_p <= p){
            break;
        }
        rand_num = rand_num + 1;
    }
    if (context->aperiodic_queue.num_jobs > 0 && rand_num == context->aperiodic_queue.num_jobs){
      rand_num = rand_num - 1;
    }

    DEBUG("rand_num %u\n", rand_num);
    next = sim_job_queue_search( &context->aperiodic_queue, &random_func, (void *) &rand_num);
  
  // next = sim_job_queue_peek(&context->aperiodic_queue);

    if (!next) {
        DEBUG("rand_num %u\n", rand_num);
        DEBUG("num_jobs %u\n", context->aperiodic_queue.num_jobs);
        DEBUG("no more jobs in queue\n");
        current_job = 0;
        return;
    }

    e = sim_event_create(sim_context_get_cur_time(context) + next->remaining_size,
                       context,
                       SIM_EVENT_JOB_DONE,
                       next);

    if (!e) {
        ERROR("failed to allocate event\n");
        return;
    }

    DEBUG("%lf switching to job %lu size %lf remaining size%lf\n", sim_context_get_cur_time(context), next->id, next->size, next->remaining_size);

    sim_event_queue_post(&context->event_queue, e);
    current_job = next;
    current_event = e;
    current_time = sim_context_get_cur_time(context);
  }

}

// Function called to print 
static void print(void* state,
                  sim_context_t* context,
                  FILE* f) {
  lottery_all_state_t* s = (lottery_all_state_t*)state;
  fprintf(f, "scheduler %s\n", s->sim->name);
}

// Map of the generic scheduler operations into specific function calls in this scheduler
// Each of these lines should be a function pointer to a function in this file
static sim_sched_ops_t ops = {
  .init = init,
  
  // all jobs are treated as aperiodic
  .periodic_job_arrival  = aperiodic_job_arrival,
  .sporadic_job_arrival  = aperiodic_job_arrival,
  .aperiodic_job_arrival = aperiodic_job_arrival,

  // job status calls
  .job_done        = job_done,
  .job_blocked     = job_blocked,
  .timer_interrupt = timer_interrupt,

  .print = print
};

// Register this scheduler with the simulation
__attribute__((constructor)) void lottery_all_init() {
  my_state.sim = sim_sched_register("lottery_all", &my_state, &ops);
}

 