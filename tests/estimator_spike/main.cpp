// Test-only, fixed-order replay. HTIF file I/O is never used by production plugins.
#include "SLAMMath.hpp"
#include "vio_config.hpp"
#include "blas_backend.hpp"
#include "gemmini_backend.hpp"
#include "clock_check.hpp"
#include "vector_check.hpp"
#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
extern "C" {extern volatile uint64_t tohost,fromhost;extern struct k_mutex htif_lock;}
namespace {
[[noreturn]] void fail(const char*s){printf("ILLIXR_ESTIMATOR_ERROR %s\n",s);fflush(stdout);k_panic();__builtin_unreachable();}
int64_t call(uint64_t op,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0,uint64_t e=0){
 alignas(64) static volatile uint64_t q[8];k_mutex_lock(&htif_lock,K_FOREVER);auto end=k_uptime_get()+5000;
 while(tohost && k_uptime_get()<end)k_yield();if(tohost)fail("host busy timeout");fromhost=0;
 q[0]=op;q[1]=a;q[2]=b;q[3]=c;q[4]=d;q[5]=e;q[6]=q[7]=0;
 asm volatile("fence rw,rw":::"memory");tohost=reinterpret_cast<uintptr_t>(q);
 while(!fromhost && k_uptime_get()<end)k_yield();asm volatile("fence rw,rw":::"memory");if(fromhost!=1)fail("host response timeout");auto ret=int64_t(q[0]);fromhost=0;k_mutex_unlock(&htif_lock);if(ret<0)fail("host syscall failed");return ret;
}
int open(const char*p,bool output){return call(56,uint64_t(int64_t(-100)),uintptr_t(p),strlen(p)+1,output?577:0,0600);}
void transfer(int fd,void*ptr,size_t n,bool write){auto*p=static_cast<char*>(ptr);while(n){auto k=call(write?64:63,fd,uintptr_t(p),n);if(!k||size_t(k)>n)fail("short/invalid transfer");p+=k;n-=k;}}
template<class T>void put(int fd,const T&v){transfer(fd,const_cast<T*>(&v),sizeof(v),true);}
}
int main(){
 using namespace ILLIXR;log_flush();replay::initialize();auto mask=clock_check::run();clock_check::platform(mask);blas_backend::initialize();if(!vector_check::run())fail("vector preflight");
 const int input=open(ILLIXR_ESTIMATOR_INPUT,false),output=open(ILLIXR_ESTIMATOR_OUTPUT,true);char magic[8];transfer(input,magic,8,false);if(memcmp(magic,"VIOSTRM1",8))fail("input version");uint64_t count;transfer(input,&count,8,false);transfer(output,const_cast<char*>("VIOSTAT1"),8,true);put(output,count);
 OpenVINS::MSCKFEstimator estimator(OpenVINS::create_vio_config());uint64_t imus=0,cameras=0,poses=0;
 for(uint64_t event=0;event<count;++event){uint32_t kind,index;int64_t ns;transfer(input,&kind,4,false);transfer(input,&index,4,false);transfer(input,&ns,8,false);
  if(kind==1){double data[6];transfer(input,data,sizeof data,false);if(index!=imus)fail("IMU ordering");estimator.feed_imu(double(ns)*1e-9,Eigen::Vector3d(data[0],data[1],data[2]),Eigen::Vector3d(data[3],data[4],data[5]));++imus;}
  else if(kind==2){uint32_t shape[2];transfer(input,shape,8,false);if(index!=cameras || !shape[0] || !shape[1] || shape[0]>752 || shape[1]>480)fail("camera ordering/shape");cv::Mat a(shape[1],shape[0],CV_8UC1),b(shape[1],shape[0],CV_8UC1);transfer(input,a.data,a.total(),false);transfer(input,b.data,b.total(),false);estimator.feed_stereo(double(ns)*1e-9,a,b);++cameras;if(estimator.is_initialized())++poses;}
  else fail("event kind");
  const auto&s=estimator.get_state();if(!s.P_full.allFinite() || !s.P_imu.allFinite() || !s.p_IinG.allFinite() || !s.q_GtoI.coeffs().allFinite())fail("nonfinite state");
  put(output,event);put(output,kind);put(output,index);put(output,ns);uint64_t initialized=estimator.is_initialized();put(output,initialized);put(output,s.timestamp);
  for(const auto*v:{&s.p_IinG,&s.v_IinG,&s.b_gyro,&s.b_accel})transfer(output,const_cast<double*>(v->data()),24,true);
  transfer(output,const_cast<double*>(s.q_GtoI.coeffs().data()),32,true);
  if(kind==2){uint64_t dim=s.P_full.rows();put(output,dim);transfer(output,const_cast<double*>(s.P_full.data()),dim*dim*8,true);transfer(output,const_cast<double*>(s.P_imu.data()),225*8,true);uint64_t clones=s.clones.size();put(output,clones);
   for(const auto&v:s.clones){put(output,v.first);put(output,v.second.timestamp);transfer(output,const_cast<double*>(v.second.q_GtoC.coeffs().data()),32,true);transfer(output,const_cast<double*>(v.second.p_CinG.data()),24,true);}
   const auto&features=estimator.replay_features();uint64_t nf=features.size();put(output,nf);for(const auto&v:features){uint64_t id=v.first,nobs=v.second.observations.size(),flags=(v.second.triangulated?1:0)|(v.second.should_marginalize?2:0);put(output,id);put(output,flags);put(output,nobs);if(v.second.triangulated)transfer(output,const_cast<double*>(v.second.p_FinG.data()),24,true);for(const auto&o:v.second.observations){put(output,o.first);transfer(output,const_cast<double*>(o.second.first.data()),16,true);transfer(output,const_cast<double*>(o.second.second.data()),16,true);}}
   printf("ILLIXR_ESTIMATOR_PROGRESS camera=%u imu=%llu poses=%llu\n",index,(unsigned long long)imus,(unsigned long long)poses);
  }
 }
 call(57,input);call(57,output);gemmini_backend::dump("estimator",false);gemmini_backend::shutdown();printf("ILLIXR_ESTIMATOR_END {\"imus\":%llu,\"cameras\":%llu,\"poses\":%llu,\"packing\":\"%s\",\"passed\":true}\n",(unsigned long long)imus,(unsigned long long)cameras,(unsigned long long)poses,ILLIXR_GEMMINI_PACKING);fflush(stdout);while(tohost)k_yield();asm volatile("fence rw,rw":::"memory");tohost=1;for(;;)asm volatile("wfi");
}
