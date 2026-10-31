// SPDX-License-Identifier: MIT
#include "gpu_frame_queue.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
static void need(bool value,const char*why){if(!value)throw std::runtime_error(why);}
int main(){try{using namespace xpfsr::gpu;FrameQueue<2>q(4096);FrameKey a{1,4096,17},b{2,4096,17};
    need(!q.reserve({0,4096,17})&&!q.reserve({1,4097,17}),"Invalid or oversized payload reserved");
    auto t=*q.reserve(a),u=*q.reserve(b);need(!q.reserve({3,4096,17})&&q.held()==2,"Queue grew beyond its memory slots");
    need(!q.take(a),"Unwritten frame was consumed instead of falling back");
    need(q.captureComplete(t)&&q.processComplete(t),"Valid completion failed");
    need(!q.take({1,2048,17})&&!q.take({1,4096,18})&&!q.take({3,4096,17}),"Wrong extent/layout/frame was consumed");
    auto consumed=*q.take(a);need(!q.reserve({3,4096,17}),"Presentation slot reused before completion");
    q.reset();need(!q.reserve({3,4096,17}),"Reset recycled in-flight GPU work");
    need(!q.captureComplete(u),"Cancelled capture was published");need(q.consumeComplete(consumed),"Cancelled presenter could not acknowledge completion");need(q.held()==0,"Cancelled work leaked slots");
    auto newer=*q.reserve({3,4096,17});need(!q.captureComplete(t)&&!q.processComplete(t)&&!q.consumeComplete(t),"Old worker acknowledgement modified a reused slot");
    need(q.captureComplete(newer),"New capture failed");q.reset();need(!q.processComplete(newer)&&q.held()==0,"Cancelled processing result resurfaced");
    auto late=*q.reserve({4,4096,17});need(q.captureComplete(late)&&q.processComplete(late),"Late fixture failed");q.discardReadyBefore(5);need(q.held()==0,"Completed stale result retained");
    FrameQueue<3>r(4096);std::atomic<bool>done{};std::thread canceller([&]{while(!done){r.reset();std::this_thread::yield();}});
    struct Join {std::atomic<bool>&done;std::thread&thread;~Join(){done=true;if(thread.joinable())thread.join();}}join{done,canceller};
    for(uint64_t f=1;f<=10000;++f){FrameKey k{f,4096,23};auto token=r.reserve(k);if(!token)continue;if(!r.captureComplete(*token))continue;if(!r.processComplete(*token))continue;auto ready=r.take(k);if(ready)need(r.consumeComplete(*ready),"Concurrent consume acknowledgement lost");}
    done=true;canceller.join();r.reset();need(r.held()==0,"Concurrent cancellation leaked a slot");
    std::cout<<"PASS: bounded frame slots, original-frame fallback, exact frame/layout matching, GPU-completion ownership, stale tokens and concurrent cancellation.\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
