#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdint.h>
extern unsigned ritnet_fixture_run(void);
extern volatile uint64_t tohost;
K_THREAD_STACK_DEFINE(heartbeat_stack,2048);
static struct k_thread heartbeat_thread;
static atomic_t heartbeats,stop;
static void heartbeat(void *a,void *b,void *c) {
 while(!atomic_get(&stop)) {atomic_inc(&heartbeats);k_msleep(1);}
}
static uint64_t cycles(void) {uint64_t v;asm volatile("rdcycle %0":"=r"(v));return v;}
static unsigned hart(void) {uintptr_t v;asm volatile("csrr %0,mhartid":"=r"(v));return v;}
struct result {uint64_t begin,end;unsigned differences,hart;};
static struct result results[32];
int main(void) {
 /* No interrupt masking: this is the primary isolated-operation fixture. */
 const uint64_t timer_begin=k_cycle_get_64();
 k_msleep(10);
 const uint64_t timer_end=k_cycle_get_64();
 k_thread_priority_set(k_current_get(),5);
 k_tid_t tid=k_thread_create(&heartbeat_thread,heartbeat_stack,
  K_THREAD_STACK_SIZEOF(heartbeat_stack),heartbeat,0,0,0,4,0,K_NO_WAIT);
 unsigned count=0;int good=timer_end>timer_begin && hart()==0;
 while(good && count<32) {
  struct result *r=&results[count++];r->hart=hart();r->begin=cycles();
  r->differences=ritnet_fixture_run();r->end=cycles();
  good=!r->differences && !r->hart;
 }
 atomic_set(&stop,1);k_thread_join(tid,K_FOREVER);
 printf("RITNET_FIXTURE_PLATFORM timer_hz=%u timer_progress=%llu hart=%u interrupts_enabled=1 heartbeats=%ld\n",
  CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC,(unsigned long long)(timer_end-timer_begin),hart(),(long)atomic_get(&heartbeats));
 for(unsigned i=0;i<count;i++)printf("RITNET_FIXTURE_RESULT iteration=%u differences=%u cycles=%llu hart=%u\n",
  i+1,results[i].differences,(unsigned long long)(results[i].end-results[i].begin),results[i].hart);
 printf("RITNET_FIXTURE_END %s\n",good && count==32?"pass":"fail");fflush(stdout);
 while(tohost)k_yield();asm volatile("fence rw,rw" ::: "memory");tohost=good&&count==32?1:3;
 for(;;)asm volatile("wfi");
}
