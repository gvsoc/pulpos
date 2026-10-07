// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

// GUI signals: the application shows what it is doing in the GVSoC GUI, with signals of its own,
// one for each kind of display.
//
// Each signal is a VCD trace declared at launch, with its place and display in the GUI: in
// config.py for the gvrun flow, with --vcd-trace options in the Makefile for the make flow. The
// application opens it by path, then gives it values or releases it (high impedance, drawn as an
// inactive line). Launch with the GUI to see them: "gvrun --target <target> run --gui" or
// "make run gui=1".

#include <stdio.h>
#if defined(__PLATFORM_GVSOC__)
#include <gvsoc.h>
#endif

// The signals only exist on GVSoC, they are ignored on the other platforms
#if defined(__PLATFORM_GVSOC__)
#define SIGNAL_OPEN(path)          gv_vcd_open_trace(path)
#define SIGNAL_SET(signal, value)  gv_vcd_dump_trace(signal, value)
#define SIGNAL_STR(signal, str)    gv_vcd_dump_trace_string(signal, (char *)(str))
#define SIGNAL_RELEASE(signal)     gv_vcd_release_trace(signal)
#else
#define SIGNAL_OPEN(path)          (-1)
#define SIGNAL_SET(signal, value)  do { (void)(signal); } while (0)
#define SIGNAL_STR(signal, str)    do { (void)(signal); } while (0)
#define SIGNAL_RELEASE(signal)     do { (void)(signal); } while (0)
#endif

#define NB_FRAMES  3
#define BUF_SIZE   16

// Kernel identifiers, as labelled by the state box of app/kernel_id
enum { KERNEL_LOAD = 1, KERNEL_CONV = 2, KERNEL_RELU = 3, KERNEL_STORE = 4 };
static const char *kernel_names[] = { "", "load", "conv", "relu", "store" };

static int sig_kernel;     // string_box: name of the running kernel
static int sig_kernel_id;  // state_box: the same as a colored label, inactive when idle
static int sig_frame;      // box (decimal): frame number
static int sig_busy;       // logic_box: BUSY while a frame is processed
static int sig_dma;        // pulse: high while a buffer is copied
static int sig_fill;       // analog (real trace, written with integers): fill level of the buffer
static int sig_macs;       // box summed on area selection: MACs of each convolution step
static int sig_log;        // string: free text messages (no spaces, VCD files separate on them)

static volatile int buffer[BUF_SIZE];
static volatile int sink;

static void work(int cycles)
{
    for (int i = 0; i < cycles; i++)
    {
        sink += i;
    }
}

static void kernel_enter(int id)
{
    SIGNAL_STR(sig_kernel, kernel_names[id]);
    SIGNAL_SET(sig_kernel_id, id);
}

// Copy the buffer in (load) or out (store): the DMA pulse is high during the copy and the buffer
// fill level goes up or down with each element
static void copy(int load)
{
    SIGNAL_SET(sig_dma, 1);
    for (int i = 0; i < BUF_SIZE; i++)
    {
        buffer[i] = load ? i : 0;
        SIGNAL_SET(sig_fill, load ? i + 1 : BUF_SIZE - i - 1);
        work(400);
    }
    SIGNAL_SET(sig_dma, 0);
}

static void conv()
{
    for (int step = 0; step < 4; step++)
    {
        // Each step does a different number of MACs, the GUI area selection sums them
        int macs = (step + 1) * 8;
        SIGNAL_SET(sig_macs, macs);
        work(macs * 80);
    }
    SIGNAL_RELEASE(sig_macs);
}

static void relu()
{
    for (int i = 0; i < BUF_SIZE; i++)
    {
        if (buffer[i] < BUF_SIZE / 2)
        {
            buffer[i] = 0;
        }
        work(160);
    }
}

int main()
{
    sig_kernel = SIGNAL_OPEN("/app/kernel");
    sig_kernel_id = SIGNAL_OPEN("/app/kernel_id");
    sig_frame = SIGNAL_OPEN("/app/frame");
    sig_busy = SIGNAL_OPEN("/app/busy");
    sig_dma = SIGNAL_OPEN("/app/dma");
    sig_fill = SIGNAL_OPEN("/app/fill");
    sig_macs = SIGNAL_OPEN("/app/macs");
    sig_log = SIGNAL_OPEN("/app/log");

    SIGNAL_SET(sig_dma, 0);
    SIGNAL_SET(sig_fill, 0);
    SIGNAL_STR(sig_log, "start");

    for (int frame = 0; frame < NB_FRAMES; frame++)
    {
        SIGNAL_SET(sig_frame, frame);
        SIGNAL_SET(sig_busy, 1);
        SIGNAL_STR(sig_log, "frame_start");

        kernel_enter(KERNEL_LOAD);
        copy(1);
        kernel_enter(KERNEL_CONV);
        conv();
        kernel_enter(KERNEL_RELU);
        relu();
        kernel_enter(KERNEL_STORE);
        copy(0);

        // Idle until the next frame: no kernel
        SIGNAL_STR(sig_kernel, "idle");
        SIGNAL_RELEASE(sig_kernel_id);
        SIGNAL_SET(sig_busy, 0);
        SIGNAL_STR(sig_log, "frame_done");
        work(4000);
    }

    SIGNAL_STR(sig_log, "end");

    printf("Processed %d frames\n", NB_FRAMES);
    return 0;
}
