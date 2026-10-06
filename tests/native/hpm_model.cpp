#include "hpm_model.hpp"
#include <cassert>
using namespace ILLIXR::hpm;
Snapshot sample(uint64_t cycle,uint64_t instructions,uint64_t misses) {
  Snapshot s;s.values[0]=cycle;s.values[1]=instructions;s.values[13]=misses;return s;
}
int main(){
  assert(delta((uint64_t(1)<<40)-2,3,13)==5);
  assert(delta(UINT64_MAX-2,3,0)==6);
  Accounting h0,h1;
  // Thread A is preempted by B, then sleeps and resumes on another hart.
  h0.charge({Owner::Openvins},sample(10,5,0),sample(40,20,2));
  h0.charge({Owner::Timewarp},sample(40,20,2),sample(60,25,2));
  h0.charge({Owner::Idle},sample(60,25,2),sample(160,26,2));
  h1.charge({Owner::Openvins},sample(1000,500,8),sample(1040,530,11));
  assert(h0.records[0].totals.values[0]+h1.records[0].totals.values[0]==70);
  assert(h0.records[0].totals.values[13]+h1.records[0].totals.values[13]==5);
  // Synchronous prediction and a remote accelerator retain distinct ownership.
  h1.charge({Owner::Prediction,Phase::Default,Owner::Timewarp},sample(1040,530,11),sample(1060,545,11));
  h0.charge({Owner::Openvins,Phase::Packing},sample(160,26,2),sample(180,40,2));
  h0.charge({Owner::Isr},sample(180,40,2),sample(190,44,2));
  h0.charge({Owner::Openvins,Phase::Accelerator},sample(190,44,2),sample(220,50,3));
  h0.charge({Owner::Profiler},sample(220,50,3),sample(224,53,3));
  uint64_t cycles=0,insns=0,misses=0;
  for(unsigned i=0;i<h0.size;++i){cycles+=h0.records[i].totals.values[0];insns+=h0.records[i].totals.values[1];misses+=h0.records[i].totals.values[13];}
  assert(cycles==214 && insns==48 && misses==3);
  assert(h0.errors==0 && h1.errors==0);
  // Exhausting bounded storage is a recorded error, never overwritten data.
  Accounting full;
  for(unsigned i=0;i<capacity;++i)full.charge({Owner(i)},sample(0,0,0),sample(1,1,0));
  full.charge({Owner(capacity)},sample(0,0,0),sample(1,1,0));
  assert(full.size==capacity && full.errors==1);
}
