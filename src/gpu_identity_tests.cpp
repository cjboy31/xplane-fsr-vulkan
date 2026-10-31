// SPDX-License-Identifier: MIT
#include "gpu_identity.hpp"
#include <iostream>
int main(){try{using namespace xpfsr::gpu;
 Identity a,b;a.device[0]=1;a.driver[0]=7;b=a;b.device[0]=2;
 if(uniqueIdentity(a,{b,a})!=1||uniqueIdentity(b,{a,b})!=1)throw std::runtime_error("Identical GPU models/order confused physical identity");
 auto rejects=[](auto f){try{f();}catch(...){return;}throw std::runtime_error("Unsafe GPU identity accepted");};
 rejects([&]{uniqueIdentity({}, {a});});rejects([&]{uniqueIdentity(a,{a,a});});rejects([&]{uniqueIdentity(a,{b});});
 auto changed=a;changed.driver[0]=8;rejects([&]{uniqueIdentity(a,{changed});});
 std::cout<<"PASS: unique physical UUID/driver matching, reordered identical models, missing/zero/ambiguous/stale identities.\n";
 return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
