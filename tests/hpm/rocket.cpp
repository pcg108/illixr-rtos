// Bare-metal instruction stream on the generated Rocket core. Frontend/cache
// protocol responses are external stimuli; all probes are observation-only.
#include "VHpmRocket.h"
#include "verilated.h"
#include <cstdint>
#include <cstdio>
#include <vector>
static uint32_t addi(unsigned d,unsigned s,int v){return ((uint32_t(v)&4095)<<20)|(s<<15)|(d<<7)|0x13;}
static uint32_t csr(unsigned addr,unsigned s,unsigned d=0){return addr<<20|s<<15|1<<12|d<<7|0x73;}
static void constant(std::vector<uint32_t>& p,unsigned d,unsigned v){p.push_back(((v+0x800)&0xfffff000)|(d<<7)|0x37);p.push_back(addi(d,d,int(v<<20)>>20));}
static unsigned failures=0,checks=0;static uint64_t coverage[13]{};
static bool run(unsigned seed){
 VHpmRocket d;
 #include "zero_inputs.h"
 std::vector<uint32_t> p;
 constant(p,5,0xfff8);p.push_back(csr(0x320,5)); // inhibit HPM, retain cycles/instret
 const unsigned selectors[]={0x101,0x201,0x401,0x801,0x1001,0x2001,0x4001,0x8001,0x10001,0x20001,0x40001,0x102,0x202};
 for(unsigned i=0;i<13;++i){constant(p,5,selectors[i]);p.push_back(csr(0x323+i,5));p.push_back(csr(0xb03+i,0));}
 constant(p,5,0x6000);p.push_back(csr(0x300,5)); // legal FP state
 p.push_back(csr(0x320,0));
 const size_t body=p.size();
 p.push_back(addi(8,0,12));p.push_back(addi(9,0,3));
 p.push_back(0x02944533); // div a0,s0,s1
 p.push_back(addi(11,10,1));
 p.push_back(0x00003603); // ld a2,0(zero), serviced by external cache responder
 p.push_back(addi(13,12,1));
 p.push_back(0x340026f3); // csrr a3,mscratch
 p.push_back(addi(14,13,1));
 p.push_back(0x00000463); // beq zero,zero,+8 (direction miss without BTB)
 p.push_back(addi(7,7,1));
 p.push_back(0x0080006f); // jal zero,+8 (target miss)
 p.push_back(addi(7,7,1));
 p.push_back(0x0000100f); // fence.i
 p.push_back(0x00000053);p.push_back(0x00000053); // dependent fadd.s; external FPU decode/ready
 p.push_back(addi(7,7,1));
 const size_t jump=p.size();int offset=int(body-jump)*4;
 const unsigned u=unsigned(offset);
 p.push_back(((u>>20&1)<<31)|((u>>1&1023)<<21)|((u>>11&1)<<20)|((u>>12&255)<<12)|0x6f);
 const uint64_t base=0x80000000ULL;uint64_t fetch=base;unsigned pending=0,tag=0,retired=0;
 bool armed=false;uint64_t previous[13]{},expected_inc[13]{};uint64_t prev_cycle=0,prev_instret=0;unsigned expected_cycle=0,expected_retire=0;
 unsigned enabled_cycles=0;uint64_t raw_previous=0;bool raw_valid=false;
 d.io_dmem_req_ready=1;d.io_dmem_ordered=1;d.io_vector_ex_ready=1;
 for(unsigned cycle=0;cycle<12000;++cycle){
  d.clock=0;d.reset=cycle<5;
  d.io_imem_resp_valid=cycle>=5&&((cycle+seed)%53>8);
  d.io_imem_resp_bits_pc=fetch;d.io_imem_resp_bits_mask=3;
  d.io_imem_resp_bits_data=(fetch>=base&&(fetch-base)/4<p.size())?p[(fetch-base)/4]:0x13;
  d.io_imem_perf_acquire=cycle%37==seed%37;d.io_dmem_perf_acquire=cycle%43==seed%43;
  d.io_dmem_req_ready=(cycle+seed)%31>7;d.io_dmem_clock_enabled=1;
  d.io_fpu_fcsr_rdy=cycle%23>4;
  d.io_dmem_resp_valid=pending==1;d.io_dmem_resp_bits_tag=tag;
  d.io_dmem_resp_bits_data=17;d.io_dmem_resp_bits_data_word_bypass=17;
  d.io_dmem_resp_bits_has_data=1;d.io_dmem_resp_bits_replay=1;d.io_dmem_s2_nack=0;
  d.eval();
  const bool fp=(d.io_fpu_inst&127)==0x53;
  d.io_fpu_dec_ren1=fp;d.io_fpu_dec_ren2=fp;d.io_fpu_dec_wen=fp;
  d.io_fpu_dec_fma=fp;d.io_fpu_dec_swap23=fp;d.eval();
  if(raw_valid && d.dbg_selectors_valid){++checks;if(d.dbg_registered_inc!=raw_previous){
   printf("HPM_SELECTOR seed=%u cycle=%u expected=%llu actual=%llu\n",seed,cycle,(unsigned long long)raw_previous,(unsigned long long)d.dbg_registered_inc);++failures;return false;}}
  raw_previous=d.dbg_events;raw_valid=d.dbg_selectors_valid&&!d.reset;
  uint64_t counts[13];
  #include "read_counts.h"
  if(armed){
   ++enabled_cycles;checks+=2;
   if(d.dbg_cycle-prev_cycle!=expected_cycle || d.dbg_instret-prev_instret!=expected_retire){
    printf("HPM_BASIC seed=%u cycle=%u\n",seed,cycle);++failures;return false;}
   for(unsigned i=0;i<13;++i){++checks;const auto difference=(counts[i]-previous[i])&((1ULL<<40)-1);
    if(difference!=expected_inc[i]){printf("HPM_DELTA seed=%u cycle=%u counter=%u expected=%llu actual=%llu\n",seed,cycle,i,(unsigned long long)expected_inc[i],(unsigned long long)difference);++failures;return false;}
   }
  }
  prev_cycle=d.dbg_cycle;prev_instret=d.dbg_instret;expected_cycle=d.dbg_cycle_run;expected_retire=d.dbg_csr_retire;
  const bool enabled=(d.dbg_inhibit&0xfff8)==0 && cycle>100;
  // Counter input is registered once in Rocket before entering CSRFile.
  for(unsigned i=0;i<13;++i){previous[i]=counts[i];expected_inc[i]=enabled?((d.dbg_registered_inc>>i)&1):0;}
  if(enabled && d.dbg_selectors_valid){armed=true;coverage[0]+=0;}
  if(armed){for(unsigned i=0;i<13;++i)coverage[i]+=expected_inc[i];}
  if(d.dbg_retire)++retired;
  if(d.dbg_exception && cycle>100){printf("HPM_EXCEPTION seed=%u cycle=%u pc=%llx cause=%llu\n",seed,cycle,(unsigned long long)d.dbg_pc,(unsigned long long)d.dbg_cause);++failures;return false;}
  if(pending) --pending;
  if(d.io_dmem_req_valid&&d.io_dmem_req_ready&&!pending){pending=9+seed%5;tag=d.io_dmem_req_bits_tag;}
  bool advance=d.io_imem_resp_ready&&d.io_imem_resp_valid;
  bool redirect=d.io_imem_req_valid;auto target=d.io_imem_req_bits_pc;
  d.clock=1;d.eval();
  if(redirect)fetch=target&0xffffffffffULL;else if(advance)fetch=(fetch&~3ULL)+4;
 }
 bool ok=armed&&enabled_cycles>1000&&retired>100;
 if(!ok){printf("HPM_INCOMPLETE seed=%u armed=%u cycles=%u retired=%u\n",seed,armed,enabled_cycles,retired);++failures;}
 return ok;
}
int main(int argc,char** argv){Verilated::commandArgs(argc,argv);
 for(unsigned seed=0;seed<8;++seed)if(!run(seed))break;
 printf("HPM_COVERAGE");for(auto v:coverage)printf(" %llu",(unsigned long long)v);puts("");
 printf("HPM_RTL checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
