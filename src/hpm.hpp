#pragma once
#include "hpm_model.hpp"
#include <zephyr/kernel.h>
#ifndef ILLIXR_HPM_PROFILE
#define ILLIXR_HPM_PROFILE 0
#endif
namespace ILLIXR::hpm {
#if ILLIXR_HPM_PROFILE
bool initialize();
void start();
void stop();
void dump();
void register_thread(k_tid_t thread, Owner owner);
void register_thread(k_tid_t thread, const char* name);
Context context();
Context exchange(Context next);
class Scope {
  Context previous_;
public:
  explicit Scope(Context next):previous_(exchange(next)){}
  ~Scope(){exchange(previous_);}
  Scope(const Scope&)=delete;
  Scope& operator=(const Scope&)=delete;
};
#else
inline bool initialize(){return true;}
inline void start(){}
inline void stop(){}
inline void dump(){}
inline void register_thread(k_tid_t,Owner){}
inline void register_thread(k_tid_t,const char*){}
inline Context context(){return {};}
inline Context exchange(Context){return {};}
class Scope { public: explicit Scope(Context){} };
#endif
}
