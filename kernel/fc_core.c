// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

// Start of the cores running the kernel other than core 0 (see pmsis/kernel/fc_core.h). The
// chip brings the start mechanism (__pi_fc_core_boot), this file the kernel side.

#include <pmsis/kernel/fc_core.h>
#include <pmsis/kernel/irq.h>
#include <pmsis/kernel/event.h>
#include <pmsis/kernel/spinlock.h>
#ifdef CONFIG_THREAD
#include <pmsis/kernel/thread.h>
#endif
#include <string.h>
#include <kernel/core_data.h>
#include <kernel/hal.h>
#if defined(CONFIG_STACK_CHECK)
#include <gvsoc.h>
#endif

typedef struct
{
    // Function executed by the core and its argument
    int (*entry)(void *arg);
    void *arg;
    // Stack of the core main thread
    uint_t stack;
    uint_t stack_size;
    // Set once the core has been started, a core can only be started once
    int started;
    // Set once the entry function has returned, with its return value in status
    volatile int done;
    volatile int status;
    // Event of the core waiting in pi_fc_core_join, and this core, notified when done is set.
    // Protected by the kernel lock.
    pi_evt_t *join_event;
    int join_core;
} pi_fc_core_boot_t;

static pi_fc_core_boot_t __pi_fc_core_boots[CONFIG_KERNEL_NB_CORES];

// Initial stack pointer of each core, read by __pi_fc_core_start_stub, the first code a started
// core executes
uint_t __pi_fc_core_sp[CONFIG_KERNEL_NB_CORES];

// Chip side: make the core start fetching at start
void __pi_fc_core_boot(int core, void (*start)(void));
// Chip side: per-core initializations, done by the core itself before its entry function
void __pi_fc_core_init_chip(int core);
// Chip side: initialization of the chip per-core variables, done by the core itself first
void __pi_fc_core_init_local(int core);
// Chip side: set the handler of the interrupt the other cores raise to notify events to the
// calling core, and enable it
void __pi_fc_core_irq_init(void (*handler)(void *arg));
// Chip side: raise this interrupt on a core
void __pi_fc_core_irq_raise(int core);
// First code a started core executes, sets the stack and calls __pi_fc_core_init
void __pi_fc_core_start_stub(void);

void __pi_evt_sched_init();
void __pi_irq_init();
void __pi_thread_sched_init();

// Events notified by other cores, which the core notifies to itself when it gets the interrupt
// those cores raise. Protected by the lock.
static PI_CORE_LOCAL pi_spinlock_t __pi_fc_core_remote_lock;
// Index of the core, 0 in the template of the per-core variables
PI_CORE_LOCAL int __pi_fc_core_self;
static PI_CORE_LOCAL pi_evt_t *__pi_fc_core_remote_first;
static PI_CORE_LOCAL pi_evt_t *__pi_fc_core_remote_last;

// Initialize the block of the per-core variables of a core from their template, and return it,
// for the core to point tp to it. Called before tp is set, so it must not use any.
char *__pi_fc_core_local_init(int core)
{
    char *block = __pi_core_local_block(core);
    uintptr_t data_size = (uintptr_t)__pi_tls_data_size;
    memcpy(block, __pi_tls_start, data_size);
    memset(block + data_size, 0, (uintptr_t)__pi_tls_size - data_size);
    return block;
}

void __pi_evt_notify_forward(pi_evt_t *event, int core)
{
    // Queue the event to the core, then interrupt it so that it notifies it
    pi_spinlock_t *lock = __PI_CORE_LOCAL_OF(__pi_fc_core_remote_lock, core);
    pi_evt_t **first = __PI_CORE_LOCAL_OF(__pi_fc_core_remote_first, core);
    pi_evt_t **last = __PI_CORE_LOCAL_OF(__pi_fc_core_remote_last, core);

    pi_spinlock_take(lock);
    event->next = NULL;
    if (*first)
    {
        (*last)->next = event;
    }
    else
    {
        *first = event;
    }
    *last = event;
    pi_spinlock_release(lock);

    __pi_fc_core_irq_raise(core);
}

static void __pi_fc_core_irq_handler(void *arg)
{
    (void)arg;

    pi_spinlock_take(&__pi_fc_core_remote_lock);
    pi_evt_t *event = __pi_fc_core_remote_first;
    __pi_fc_core_remote_first = NULL;
    pi_spinlock_release(&__pi_fc_core_remote_lock);

    // The callbacks get executed when leaving the interrupt handler
    while (event)
    {
        pi_evt_t *next = event->next;
        __pi_evt_notify_local(event);
        event = next;
    }
}

void __pi_fc_core_kernel_init()
{
    pi_spinlock_init(&__pi_fc_core_remote_lock);
    __pi_fc_core_remote_first = NULL;
    __pi_fc_core_irq_init(__pi_fc_core_irq_handler);
}

void __attribute__((noreturn)) __pi_fc_core_init(int core)
{
    pi_fc_core_boot_t *boot = &__pi_fc_core_boots[core];

    // From now on, the core works on its own per-core variables
    char *block = __pi_fc_core_local_init(core);
    asm volatile ("mv tp, %0" : : "r" (block) : "memory");
    __pi_fc_core_self = core;
    __pi_fc_core_init_local(core);

#if defined(CONFIG_STACK_CHECK)
    gv_stack_set((void *)boot->stack, boot->stack_size);
#endif

#ifdef CONFIG_EVENT
    __pi_evt_sched_init();
#endif

    __pi_irq_init();
    __pi_fc_core_kernel_init();

#ifdef CONFIG_THREAD
    __pi_thread_sched_init();
    // The core main thread runs on the stack it was given
    __pi_thread_main.event = NULL;
#if defined(CONFIG_STACK_CHECK)
    __pi_thread_main.stack_base = boot->stack;
    __pi_thread_main.stack_size = boot->stack_size;
#endif
#endif

    __pi_fc_core_init_chip(core);

    __pi_irq_global_enable();

    boot->status = boot->entry(boot->arg);

    int irq = __pi_kernel_lock_irq();
    boot->done = 1;
    pi_evt_t *join_event = boot->join_event;
    int join_core = boot->join_core;
    __pi_kernel_unlock_irq(irq);

    // The joining core is another one
    if (join_event)
    {
        pi_evt_notify_remote(join_event, join_core);
    }

#ifdef CONFIG_THREAD
    // The core keeps running the threads it created, if any, and otherwise sleeps while
    // handling its interrupts
    pi_thread_exit();
#endif
    while (1)
    {
        asm volatile ("wfi");
    }
}

int pi_fc_core_start(int core, int (*entry)(void *arg), void *arg, void *stack,
    unsigned int stack_size)
{
    if (core <= 0 || core >= CONFIG_KERNEL_NB_CORES)
    {
        return -1;
    }

    pi_fc_core_boot_t *boot = &__pi_fc_core_boots[core];

    int irq = pi_irq_lock();
    if (boot->started)
    {
        pi_irq_unlock(irq);
        return -1;
    }
    boot->started = 1;
    pi_irq_unlock(irq);

    boot->entry = entry;
    boot->arg = arg;
    boot->stack = (uint_t)stack;
    boot->stack_size = stack_size;
    boot->done = 0;
    boot->join_event = NULL;
    __pi_fc_core_sp[core] = (uint_t)stack + stack_size;

    __pi_fc_core_boot(core, __pi_fc_core_start_stub);

    return 0;
}

int pi_fc_core_join(int core)
{
    pi_fc_core_boot_t *boot = &__pi_fc_core_boots[core];
    pi_evt_t event;

    // Unless the core is already done, give it an event to notify when it is
    int irq = __pi_kernel_lock_irq();
    int done = boot->done;
    if (!done)
    {
        boot->join_event = pi_evt_sig_init(&event);
        boot->join_core = pi_fc_core_id();
    }
    __pi_kernel_unlock_irq(irq);

    if (!done)
    {
        pi_evt_sig_wait(&event);
    }

    return boot->status;
}
