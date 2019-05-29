//
// time.h
//
// Time types
//
// Copyright (C) 2002 Michael Ringgaard. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 
// 1. Redistributions of source code must retain the above copyright 
//    notice, this list of conditions and the following disclaimer.  
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.  
// 3. Neither the name of the project nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission. 
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
// SUCH DAMAGE.
// 

#if _MSC_VER > 1000
#pragma once
#endif

#ifndef SYS_TIME_H
#define SYS_TIME_H

#include <sys/types.h>

#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED

struct timeval {
  long tv_sec;                  // Seconds
  long tv_usec;                 // Microseconds
};

#endif

struct itimerval {
  struct timeval it_interval;   // Timer interval
  struct timeval it_value;      // Current value  
};

#define ITIMER_REAL    0           // A SIGALRM signal is delivered when this timer expires
#define ITIMER_VIRTUAL 1           // A SIGVTALRM signal is delivered when this timer expires
#define ITIMER_PROF    2           // A SIGPROF signal is delivered when this timer expires

#ifdef  __cplusplus
extern "C" {
#endif

int getitimer(int which, struct itimerval *value);
int setitimer(int which, const struct itimerval *value, struct itimerval *oldvalue);

osapi int gettimeofday(struct timeval *tv, void *tzp);

#ifdef  __cplusplus
}
#endif

#endif
