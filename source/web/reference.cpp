// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Mario Brandalise Baril
#include "engine.h"
#include <cstdio>
#include <iostream>
#include <fstream>
#include <sstream>
// Text commands on stdin make exactly the same rendering scenario reproducible in Node/WASM.
extern "C" {
int web_init(double,int);void web_set(int,float);void web_commit();void web_midi(int,int,int);
void web_render(int);float* web_output(int);void web_route(int,int,int,float);
}
int main(int argc,char** argv) {
    if(argc>1&&std::string(argv[1])=="schema"){std::cout<<usina::web::metadata();return 0;}
    if(argc<3)return 2;
    std::ifstream commands(argv[1]);std::ofstream output(argv[2],std::ios::binary);
    if(!commands||!output)return 3;
    std::string line;
    while(std::getline(commands,line)) {
        std::istringstream s(line);std::string op;s>>op;
        if(op=="init"){double rate;int os;s>>rate>>os;if(!web_init(rate,os))return 4;}
        else if(op=="set"){int i;float v;s>>i>>v;web_set(i,v);}
        else if(op=="commit")web_commit();
        else if(op=="midi"){int a,b,c;s>>a>>b>>c;web_midi(a,b,c);}
        else if(op=="route"){int i,a,b;float c;s>>i>>a>>b>>c;web_route(i,a,b,c);}
        else if(op=="render"){int n;s>>n;while(n>0){int k=std::min(n,128);web_render(k);for(int i=0;i<k;++i)for(int c=0;c<2;++c){float v=web_output(c)[i];if(!std::isfinite(v)||std::abs(v)>1)return 5;output.write((const char*)&v,4);}n-=k;}}
    }
}
