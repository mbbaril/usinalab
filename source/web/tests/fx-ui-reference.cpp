// SPDX-License-Identifier: GPL-3.0-only
// Build against the existing DSP only to compare display strings, without changing any audio target.
#include "fx/Distort.h"
#include <cstdio>
int main(){for(int mode=0;mode<9;++mode)for(int i=0;i<=1000;++i)std::printf("%d\t%d\t%s\t%s\n",mode,i,usina::fx::Distort::characterName(mode),usina::fx::Distort::characterText(mode,i/1000.0).c_str());}
