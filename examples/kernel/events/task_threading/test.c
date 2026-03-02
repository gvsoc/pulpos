// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <stdio.h>
#include <stdint.h>
#include <pmsis/kernel/thread.h>
#include <pmsis/kernel/event.h>
#include <pmsis/kernel/time.h>

#define PERIOD0 10000
#define STACK_SIZE 2048

static uint8_t stack0[STACK_SIZE];
static uint8_t stack1[STACK_SIZE];

static int count0 = 50;
static pi_evt_t end_event;
static volatile int end = 0;

static void thread0_entry(void *arg)
{
    while(!end)
    {
        pi_time_wait_us(1000);
    }
}

static void thread1_entry(void *arg)
{
    while(!end)
    {
        pi_time_wait_us(1000);
    }
}

static void delay_handler(pi_evt_t *event)
{
    count0--;
    if (count0 > 0)
    {
        pi_evt_notify_delayed(event, PERIOD0);
    }
    else
    {
        pi_evt_notify(&end_event);
    }
    printf("Delay 0 handler\n");
}

static void task0_handler(pi_evt_t *event)
{
    for (int i=0; i<10; i++)
    {
        printf("Task 0 executing\n");

        for (volatile int j=0; j<1000000; j++);
    }
}

static void task1_handler(pi_evt_t *event)
{
    for (int i=0; i<10; i++)
    {
        printf("Task 1 executing\n");

        for (volatile int j=0; j<1000000; j++);
    }
}

int main()
{
    pi_evt_t task0_event, task1_event, delayed_event, event0, event1;
    pi_thread_t thread0, thread1;

    printf("Entered example\n");

    if (pi_thread_create(&thread0, "thread0", thread0_entry, NULL, 0, stack0,
            STACK_SIZE, pi_evt_sig_init(&event0))) return -1;

    if (pi_thread_create(&thread1, "thread1", thread1_entry, NULL, 0, stack1,
            STACK_SIZE, pi_evt_sig_init(&event1))) return -1;

    pi_evt_sig_init(&end_event);

    pi_evt_notify(pi_evt_task_init(&task0_event, task0_handler, &thread0));
    pi_evt_notify(pi_evt_task_init(&task1_event, task1_handler, &thread1));

    pi_evt_notify_delayed(pi_evt_cb_init(&delayed_event, delay_handler), PERIOD0);

    pi_evt_sig_wait(&end_event);

    end = 1;
    pi_evt_sig_wait(&event0);
    pi_evt_sig_wait(&event1);

    return 0;
}
