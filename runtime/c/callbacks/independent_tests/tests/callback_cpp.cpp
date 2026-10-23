/* SPDX-License-Identifier: MIT. Independent C++17 public ABI consumer. */
#include "doe_callbacks.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>
static_assert(std::is_standard_layout<doe_callback_request>::value,"public C record");
static_assert(sizeof(doe_callback_request)==88 && alignof(doe_callback_request)==8,"x64 ABI");
static_assert(sizeof(doe_callback_owner)==32 && sizeof(doe_callback_allocator)==32,"x64 ABI");
struct State { unsigned refs=1, delivered=0, copied=0, allocated=0, freed=0; doe_future_queue *queue=nullptr; };
struct alignas(128) Context { State *state; unsigned value; char bytes[71]; };
static const int identity=5;
static void *allocate(void *p,const void *v,size_t n,size_t a) { assert(v==&identity); auto*s=static_cast<State*>(p); s->allocated++; void *out=nullptr; assert(posix_memalign(&out,a,n)==0); return out; }
static void deallocate(void*p,const void*v,void*memory,size_t,size_t a) { assert(v==&identity && a==alignof(Context)); auto*s=static_cast<State*>(p); assert(s->delivered==1); s->freed++; std::free(memory); }
static void retain(void*p) { ++static_cast<State*>(p)->refs; }
static void release(void*p) { auto*s=static_cast<State*>(p); assert(s->refs>1); --s->refs; }
static void copy(void*d,const void*p) { auto*c=static_cast<const Context*>(p); assert(reinterpret_cast<uintptr_t>(d)%alignof(Context)==0); ::new(d) Context(*c); ++c->state->copied; }
static void deliver(void*p) { auto*c=static_cast<Context*>(p); assert(c->value==291 && std::strcmp(c->bytes,"typed C++ placement initialization survives source overwrite")==0); ++c->state->delivered; c->~Context(); }
int main() { State s; doe_future_storage_requirements requirements{}; assert(doe_future_queue_requirements(1,&requirements)==DOE_FUTURE_OK); void*storage=nullptr; assert(posix_memalign(&storage,requirements.alignment,requirements.size)==0); assert(doe_future_queue_init(1,storage,requirements.size,&s.queue)==DOE_FUTURE_OK); doe_callback_owner owner{s.queue,&s,retain,release}; Context context{&s,291,"typed C++ placement initialization survives source overwrite"}; doe_callback_request r{1,2,&owner,{&s,&identity,allocate,deallocate},&context,sizeof(context),alignof(Context),copy,deliver}; uint64_t id=0; assert(doe_callback_ready(&r,&id)==DOE_CALLBACK_OK); context.value=0; std::memset(context.bytes,0,sizeof(context.bytes)); assert(s.refs==2 && s.delivered==0); assert(doe_future_process_events(s.queue)==DOE_FUTURE_OK); assert(s.refs==1 && s.delivered==1 && s.copied==1 && s.allocated==1 && s.freed==1); doe_future_wait_info w{id,0}; assert(doe_future_wait_any(s.queue,&w,1,0)==DOE_FUTURE_OK && w.completed==1); assert(doe_future_queue_deinit(s.queue)==DOE_FUTURE_OK); std::free(storage); return 0; }
