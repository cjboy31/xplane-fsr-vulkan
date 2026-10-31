// SPDX-License-Identifier: MIT
#include "nr_pass_policy.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
static void need(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main(){try{using namespace xpfsr::nr;Controls c;
    for(int detail=0;detail<=200;++detail){c[Detail]=detail;c[Passes]=1;
        for(int capacity=1;capacity<=3;++capacity)need(multipassDetailStrength(c,capacity)==detail/100.f,"One-pass composition changed");
        float previous=detail/100.f;
        for(int passes=1;passes<=3;++passes){c[Passes]=passes;auto saved=c;float strength=multipassDetailStrength(c,3);
            need(std::isfinite(strength)&&strength>=0&&strength<=previous,"Extra passes increased the detail composition budget");
            need(std::abs(strength*passes-detail/100.f)<1e-6f,"Total detail budget drifted");
            need(c==saved,"Calculating effective controls changed saved choices");previous=strength;
            need(effectivePasses(c,1)==1&&multipassDetailStrength(c,1)==detail/100.f,"A one-pass context was weakened by an unavailable requested pass");}
    }
    std::cout<<"PASS: one-pass compatibility, bounded multi-pass detail budget, allocated-capacity clamping and unchanged user controls.\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
