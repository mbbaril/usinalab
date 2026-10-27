// SPDX-License-Identifier: GPL-3.0-only
// Display-only measurement. Uses the unchanged filter kernels; never runs in the audio processor.
#include "engines/fm/FmVoiceFilter.h"
#include <complex>
#include <vector>
#include <limits>
#include <emscripten/emscripten.h>

namespace {
constexpr int N=131072, B=321;
float result[B];
using C=std::complex<double>;
void fft(std::vector<C>& a) {
    for(int i=1,j=0;i<N;i++){int bit=N>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
    for(int len=2;len<=N;len<<=1){C wlen=std::polar(1.0,-2*3.14159265358979323846/len);
        for(int i=0;i<N;i+=len){C w=1;for(int j=0;j<len/2;j++){C u=a[i+j],v=a[i+j+len/2]*w;a[i+j]=u+v;a[i+j+len/2]=u-v;w*=wlen;}}}
}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE float* filter_result(){return result;}
// Returns 0 for a settled small-signal measurement, 1 for an unstable/non-stationary response.
EMSCRIPTEN_KEEPALIVE int filter_measure(int type,double cut,double res,double drive,double hp,double peak,double fb,int hot,int life,double rate,int firmware){
    using namespace usina::fm;
    rate=std::clamp(rate,22050.0,768000.0);type=std::clamp(type,0,17);
    VoiceFilter f[2];Casc c[2];for(int j=0;j<2;j++){f[j].prepare(rate,firmware);c[j].prepare(rate,firmware);}
    const double lg=std::log2(6.489)+std::clamp(cut,0.0,1000.0)*std::log2(23680.0/6.489)/1000;
    const double hlg=std::log2(6.489)+std::clamp(hp,0.0,1000.0)*std::log2(23680.0/6.489)/1000;
    const double gain=std::exp2(3*std::clamp(drive,0.0,100.0)/100),amp=0.0001;
    std::vector<C> a(N);double energy=0,tail=0;
    for(int i=0;i<N;i++){
        double y[2];for(int j=0;j<2;j++){double x=(i==0&&j==0)?amp:0;
            y[j]=type==kCascType?c[j].process(x*gain,hlg,lg,peak,res,fb*0.02,hot):f[j].process(x,lg,type,res,gain,life);}
        double v=(y[0]-y[1])/amp;if(!std::isfinite(v))return 1;
        a[i]=v;energy+=v*v;if(i>=N*7/8)tail+=v*v;
    }
    // Do not present self-oscillation or a growing transient as a frequency response.
    if(tail>1e-8 && tail>energy*0.001)return 1;
    fft(a);
    for(int i=0;i<B;i++){
        double hz=6.489*std::pow(23680.0/6.489,double(i)/(B-1)),bin=std::min(hz*N/rate,double(N/2-1));
        if(hz>=rate*0.5){result[i]=std::numeric_limits<float>::quiet_NaN();continue;}
        int k=int(bin);double t=bin-k,mag=std::abs(a[k]*(1-t)+a[k+1]*t);
        result[i]=float(std::clamp(20*std::log10(std::max(mag,1e-8)),-96.0,48.0));
    }
    return 0;
}
}
