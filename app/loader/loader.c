/*
 * Copyright (c) 2015 Carlos Pizano-Uribe <cpu@chromium.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include <lib/tftp.h>
#include <lib/cksum.h>
#include <lib/elf.h>

#include <kernel/thread.h>

#if defined(WITH_LIB_CONSOLE)
#include <lib/console.h>
#else
#error "loader app needs a console"
#endif

#if defined(SDRAM_BASE)
static unsigned char* download_start = (void*)SDRAM_BASE;
#else
static unsigned char* download_start = NULL;
#endif

#define FNAME_SIZE 64
#define DOWNLOAD_SLOT_SIZE (128 * 1024)

typedef enum {
    DOWNLOAD_ANY,
    DOWNLOAD_ELF,
} download_type;

typedef struct {
    unsigned char* start;
    unsigned char* end;
    unsigned char* max;
    char name[FNAME_SIZE];
    download_type type;
} download_t;

static download_t* make_download(const char* name)
{
    download_t* s = malloc(sizeof(download_t));
    s->start = download_start;
    s->end = s->start;
    s->max = download_start + DOWNLOAD_SLOT_SIZE;
    strncpy(s->name, name, FNAME_SIZE);

    download_start = s->max;
    memset(s->start, 0, DOWNLOAD_SLOT_SIZE);
    return s;
}

static size_t output_result(const download_t* download)
{
    size_t len = download->end - download->start;
    unsigned long crc = crc32(0, download->start, len);
    printf("[%s] done, start at: %p - %zu bytes, crc32 = %lu\n",
           download->name, download->start, len, crc);
    return len;
}

static int run_elf(void* entry_point)
{
    void (*elf_start)(void) = (void*)entry_point;
    printf("elf (%p) running ...\n", entry_point);
    thread_sleep(10);
    elf_start();
    printf("elf (%p) finished\n", entry_point);
    return 0;
}

static void process_elf_blob(const void* start, size_t len)
{
    elf_handle_t elf;
    status_t st = elf_open_handle_memory(&elf, start, len);
    if (st < 0) {
        printf("unable to open elf handle\n");
        return;
    }

    st = elf_load(&elf);
    if (st < 0) {
        printf("elf processing failed, status : %d\n", st);
        elf_close_handle(&elf);
        return;
    }

    printf("elf looks good\n");
    thread_resume(thread_create("elf_runner", &run_elf, (void*)elf.entry,
                                DEFAULT_PRIORITY, DEFAULT_STACK_SIZE));
    elf_close_handle(&elf);
}

int tftp_callback(void* data, size_t len, void* arg)
{
    download_t* download = arg;
    size_t final_len;

    if (!data) {
        final_len = output_result(download);
        if (download->type == DOWNLOAD_ELF) {
            process_elf_blob(download->start, final_len);
        }

        download->end = download->start;
        return 0;
    }

    if ((download->start + len) > download->max) {
        printf("transfer too big, aborting\n");
        return -1;
    }
    if (len) {
        memcpy(download->end, data, len);
        download->end += len;
    }
    return 0;
}

static int loader(int argc, const cmd_args *argv)
{
    download_t* download;

    if (!download_start) {
        printf("loader not available. it needs sdram\n");
        return 0;
    }

    if (argc != 3) {
usage:
        printf("load any [filename]\n" \
               "load elf [filename]\n");
        return 0;
    }

    download = make_download(argv[2].str);

    if (strcmp(argv[1].str, "any") == 0) {
        download->type = DOWNLOAD_ANY;
    } else if (strcmp(argv[1].str, "elf") == 0) {
        download->type = DOWNLOAD_ELF;
    } else {
        goto usage;
    }

    tftp_set_write_client(download->name, &tftp_callback, download);
    printf("ready for %s over tftp\n", argv[2].str);
    return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("load", "download and run via tftp", &loader)
STATIC_COMMAND_END(loader);

