/**************************************************************
 *
 * userprog/ksyscall.h
 *
 * Kernel interface for systemcalls 
 *
 * by Marcus Voelp  (c) Universitaet Karlsruhe
 *
 **************************************************************/

#ifndef __USERPROG_KSYSCALL_H__ 
#define __USERPROG_KSYSCALL_H__ 

#include "kernel.h"



void SysHalt()
{
    kernel->interrupt->Halt();
}


int SysAdd(int op1, int op2)
{
    return op1 + op2;
}

void SysPrintInt(int number)
{
    kernel->interrupt->PrintInt(number);
}

void SysNice(int priority)
{
    kernel->interrupt->Nice(priority);
}

#endif /* ! __USERPROG_KSYSCALL_H__ */
