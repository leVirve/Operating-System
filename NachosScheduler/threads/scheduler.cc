// scheduler.cc 
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would 
//	end up calling FindNextToRun(), and that would put us in an 
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "debug.h"
#include "scheduler.h"
#include "main.h"

//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads.
//	Initially, no ready threads.
//----------------------------------------------------------------------

Scheduler::Scheduler()
{ 
    readyRRList = new List<Thread *>("RR");
    readyPriorityList = new SortedList<Thread *>("Priority", Thread::compare_by_priority);
    readySJFList = new SortedList<Thread *>("SJF", Thread::compare_by_burst);
    toBeDestroyed = NULL;
} 

//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler()
{ 
    delete readyRRList; 
    delete readyPriorityList;
    delete readySJFList;
}

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

void
Scheduler::ReadyToRun (Thread *thread)
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());
    
    thread->setStatus(READY); 
    thread->setStartReadyTime(kernel->stats->totalTicks);

    cout << "Tick " << kernel->stats->totalTicks << " Thread " << thread->getID() << " ";

    int pri = thread->getPriority();
    if (pri >= SJF_SCHD_THRESHHOLD) {
        readySJFList->Append(thread);
    } else if (pri >= PRI_SCHD_THRESHHOLD) {
        readyRRList->Append(thread);
    } else {
        readyPriorityList->Append(thread);
    }
    cout << "Thread " <<  thread->getID() << "\tProcessReady\t" << kernel->stats->totalTicks << endl;
}

void
Scheduler::aging (List<Thread *>* readylist)
{
    SortedList<Thread *>* list = dynamic_cast<SortedList<Thread *>*>(readylist);
    ListIterator<Thread *> *iter = new ListIterator<Thread *>((List<Thread *>*) list);
    for (; !iter->IsDone(); iter->Next()) {
        Thread* thread = iter->Item();
        int clocks_interval = kernel->stats->totalTicks - thread->getStartReadyTime();
        if (clocks_interval >= AGING_TICKS) {
            thread->setPriority(PRIORITY_AGING + thread->getPriority());
            thread->setStartReadyTime(kernel->stats->totalTicks);
            list->Remove(thread);
            list->Insert(thread);
        }
    }
    processMoving();
}

void
Scheduler::processMoving ()
{
    ListIterator<Thread *>* iterRR  = new ListIterator<Thread *>((List<Thread *>*) readyRRList);
    ListIterator<Thread *> *iterPri = new ListIterator<Thread *>((List<Thread *>*) readyPriorityList);
    ListIterator<Thread *> *iterSJF = new ListIterator<Thread *>((List<Thread *>*) readySJFList);
    
    for (; !iterSJF->IsDone(); iterSJF->Next()) {
        Thread* thread = iterSJF->Item();
        if (thread->getPriority() < SJF_SCHD_THRESHHOLD) {
            readySJFList->Remove(thread);
            ReadyToRun(thread);
        }
    }

    for (; !iterRR->IsDone(); iterRR->Next()) {
        Thread* thread = iterRR->Item();
        if (thread->getPriority() < PRI_SCHD_THRESHHOLD
                || thread->getPriority() >= SJF_SCHD_THRESHHOLD) {
            readyRRList->Remove(thread);
            ReadyToRun(thread);
        }
    }

    for (; !iterPri->IsDone(); iterPri->Next()) {
        Thread* thread = iterPri->Item();
        if (thread->getPriority() >= PRI_SCHD_THRESHHOLD) {
            readyPriorityList->Remove(thread);
            ReadyToRun(thread);
        }
    } 
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

Thread *
Scheduler::FindNextToRun ()
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    Thread * nextThread = NULL;

    aging(readyPriorityList);

    if (!readySJFList->IsEmpty()) {
        nextThread = readySJFList->RemoveFront();
    } else if (!readyRRList->IsEmpty()) {
        nextThread = readyRRList->RemoveFront();
    } else if (!readyPriorityList->IsEmpty()) {
        nextThread = readyPriorityList->RemoveFront();
    }
    return nextThread;
}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable kernel->currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//	"finishing" is set if the current thread is to be deleted
//		once we're no longer running on its stack
//		(when the next thread starts running)
//----------------------------------------------------------------------

void
Scheduler::Run (Thread *nextThread, bool finishing)
{
    Thread *oldThread = kernel->currentThread;

    if(oldThread == nextThread) return;

    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (finishing) {	// mark that we need to delete current thread
        ASSERT(toBeDestroyed == NULL);
        toBeDestroyed = oldThread;
    }

    if (oldThread->space != NULL) {	// if this thread is a user program,
        oldThread->SaveUserState(); 	// save the user's CPU registers
        oldThread->space->SaveState();
    }

    oldThread->CheckOverflow();		    // check if the old thread
                                            // had an undetected stack overflow

    // old thread burst time
    double predicted = 0.5 * (kernel->stats->totalTicks - oldThread->getStartBurst() + oldThread->getBurstTime());
    oldThread->setBurstTime(predicted);
    // set up the individual start time of burst
    nextThread->setStartBurstTime(kernel->stats->totalTicks);

    kernel->currentThread = nextThread;  // switch to the next thread
    nextThread->setStatus(RUNNING);      // nextThread is now running
    cout << "Thread " << kernel->currentThread->getID() << "\tProcessRunning\t" << kernel->stats->totalTicks << endl;

    DEBUG(dbgThread, "Switching from: " << oldThread->getName() << " to: " << nextThread->getName());

    // This is a machine-dependent assembly language routine defined 
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".

    SWITCH(oldThread, nextThread);

    // we're back, running oldThread

    // interrupts are off when we return from switch!
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    DEBUG(dbgThread, "Now in thread: " << oldThread->getName());

    CheckToBeDestroyed();		// check if thread we were running
    // before this one has finished
    // and needs to be cleaned up

    if (oldThread->space != NULL) {	    // if there is an address space
        oldThread->RestoreUserState();     // to restore, do it.
        oldThread->space->RestoreState();
    }
}

//----------------------------------------------------------------------
// Scheduler::CheckToBeDestroyed
// 	If the old thread gave up the processor because it was finishing,
// 	we need to delete its carcass.  Note we cannot delete the thread
// 	before now (for example, in Thread::Finish()), because up to this
// 	point, we were still running on the old thread's stack!
//----------------------------------------------------------------------

void
Scheduler::CheckToBeDestroyed()
{
    if (toBeDestroyed != NULL) {
        delete toBeDestroyed;
        toBeDestroyed = NULL;
    }
}

//----------------------------------------------------------------------
// Scheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
void
Scheduler::Print()
{
    cout << "Ready list contents:\n";
    readyPriorityList->Apply(ThreadPrint);
    cout << endl;
}

