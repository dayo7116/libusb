/*
 * libusb example program to measure Atmel SAM3U isochronous performance
 * Copyright (C) 2012 Harald Welte <laforge@gnumonks.org>
 *
 * Copied with the author's permission under LGPL-2.1 from
 * http://git.gnumonks.org/cgi-bin/gitweb.cgi?p=sam3u-tests.git;a=blob;f=usb-benchmark-project/host/benchmark.c;h=74959f7ee88f1597286cd435f312a8ff52c56b7e
 *
 * An Atmel SAM3U test firmware is also available in the above repository.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <config.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <time.h>

#include <thread>

#include "libusb.h"
#include "libusbi.h"

#include <string>
#include <chrono>

#define EP_DATA_IN 0x82
#define EP_ISO_IN 0

static volatile sig_atomic_t do_exit = 0;
static struct libusb_device_handle *devh = NULL;

static unsigned long num_bytes = 0, num_xfer = 0;
static struct timeval tv_start;

static void get_timestamp(struct timeval *tv) {
#if defined(PLATFORM_WINDOWS)
  static LARGE_INTEGER frequency;
  LARGE_INTEGER counter;

  if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);

  QueryPerformanceCounter(&counter);
  counter.QuadPart *= 1000000;
  counter.QuadPart /= frequency.QuadPart;

  tv->tv_sec = (long)(counter.QuadPart / 1000000ULL);
  tv->tv_usec = (long)(counter.QuadPart % 1000000ULL);
#elif defined(HAVE_CLOCK_GETTIME)
  struct timespec ts;

  (void)clock_gettime(CLOCK_MONOTONIC, &ts);
  tv->tv_sec = ts.tv_sec;
  tv->tv_usec = (int)(ts.tv_nsec / 1000L);
#else
  gettimeofday(tv, NULL);
#endif
}

static void LIBUSB_CALL cb_xfr_out(struct libusb_transfer *xfr) {
  int i;

  if (xfr->status != LIBUSB_TRANSFER_COMPLETED) {
    fprintf(stderr, "transfer status %d\n", xfr->status);
    libusb_free_transfer(xfr);
    exit(3);
  }

  if (xfr->type == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) {
    for (i = 0; i < xfr->num_iso_packets; i++) {
      struct libusb_iso_packet_descriptor *pack = &xfr->iso_packet_desc[i];

      if (pack->status != LIBUSB_TRANSFER_COMPLETED) {
        fprintf(stderr, "Error: pack %d status %d\n", i, pack->status);
        exit(5);
      }

      printf("pack%d length:%u, actual_length:%u\n", i, pack->length,
             pack->actual_length);
    }
  }

  printf("send length:%u, actual_length:%u, %s, total:%d\n", xfr->length,
         xfr->actual_length, xfr->buffer, num_bytes);
  num_bytes += xfr->actual_length;
  num_xfer++;
   libusb_free_transfer(xfr);
}

static void LIBUSB_CALL cb_xfr_in(struct libusb_transfer *xfr) {
  int i;

  if (xfr->status != LIBUSB_TRANSFER_COMPLETED) {
    fprintf(stderr, "transfer status %d\n", xfr->status);
    libusb_free_transfer(xfr);
    exit(3);
  }

  if (xfr->type == LIBUSB_TRANSFER_TYPE_ISOCHRONOUS) {
    for (i = 0; i < xfr->num_iso_packets; i++) {
      struct libusb_iso_packet_descriptor *pack = &xfr->iso_packet_desc[i];

      if (pack->status != LIBUSB_TRANSFER_COMPLETED) {
        fprintf(stderr, "Error: pack %d status %d\n", i, pack->status);
        exit(5);
      }

      printf("pack%d length:%u, actual_length:%u\n", i, pack->length,
             pack->actual_length);
    }
  }

  printf("receive length:%u, actual_length:%u, %s, total:%d\n", xfr->length,
         xfr->actual_length, xfr->buffer, num_bytes);
  num_bytes += xfr->actual_length;
  num_xfer++;
  libusb_free_transfer(xfr);
}

static int benchmark_out(uint8_t ep) {
  static int s_count = 0;

  struct libusb_transfer *xfr = libusb_alloc_transfer(0);
  if (!xfr) {
    errno = ENOMEM;
    return -1;
  }

  s_count++;
  std::string msg = "hello world from pc " + std::to_string(s_count);

  const int buf_length = 128;
  uint8_t *buf = new uint8_t[buf_length];
  memset(buf, 0, buf_length * sizeof(uint8_t));
  strcpy((char *)buf, msg.c_str());

  xfr->flags |= LIBUSB_TRANSFER_FREE_BUFFER;
  libusb_fill_bulk_transfer(xfr, devh, ep, buf, buf_length, cb_xfr_out, NULL,
                            0);
  int submit = libusb_submit_transfer(xfr);
  if (0 != submit) {
    printf("");
  }
  return submit;
}

static int benchmark_in(uint8_t ep) {
  struct libusb_transfer *xfr = libusb_alloc_transfer(0);

  if (!xfr) {
    errno = ENOMEM;
    return -1;
  }

  const int buf_length = 128;
  uint8_t *buf = new uint8_t[buf_length];
  memset(buf, 0, buf_length * sizeof(uint8_t));

  xfr->flags |= LIBUSB_TRANSFER_FREE_BUFFER;
  libusb_fill_bulk_transfer(xfr, devh, ep, buf, buf_length, cb_xfr_in, NULL,
                            0);
  int submit = libusb_submit_transfer(xfr);
  if (0 != submit) {
    printf("");
  }
  return submit;
}

static void measure(void) {
  struct timeval tv_stop;
  unsigned long diff_msec;

  get_timestamp(&tv_stop);

  diff_msec = (tv_stop.tv_sec - tv_start.tv_sec) * 1000L;
  diff_msec += (tv_stop.tv_usec - tv_start.tv_usec) / 1000L;

  printf(
      "%lu transfers (total %lu bytes) in %lu milliseconds => %lu bytes/sec\n",
      num_xfer, num_bytes, diff_msec, (num_bytes * 1000L) / diff_msec);
}

static void sig_hdlr(int signum) {
  (void)signum;

  measure();
  do_exit = 1;
}

void printDev() {
  uint8_t i, j;
  struct libusb_device_descriptor desc;
  struct libusb_config_descriptor *config;
  struct libusb_interface_descriptor interface;
  int r = libusb_get_device_descriptor(devh->dev, &desc);  //获取设备描述符
  if (r < 0) {
    printf("failed to get device descriptor\n");
    return;
  }
  r = libusb_get_config_descriptor(devh->dev, 0, &config);  //获取配置描述符
  if (r < 0) {
    printf("failed to get config descriptor\n");
    return;
  }
  if (desc.iProduct) {
    unsigned char string[256];
    int ret = libusb_get_string_descriptor_ascii(devh, desc.iProduct, string,
                                             sizeof(string));
    if (ret > 0) printf("  Product:                   %s\n", (char *)string);
  }
  for (i = 0; i < config->bNumInterfaces; i++) {  //遍历设备接口，找到ADB接口；一个设备可有多个不同功能的接口
    for (j = 0; j < config->interface[i].num_altsetting; j++) {
      interface = config->interface[i].altsetting[j];

      printf(
          "interface:%d, %d class:%#x, subclass:%#x, protocol:%#x, bInterfaceNumber:%d, endpoint count:%d",
          i, j, interface.bInterfaceClass, interface.bInterfaceSubClass,
          interface.bInterfaceProtocol, interface.bInterfaceNumber, interface.bNumEndpoints);
      
      for (int k = 0; k < interface.bNumEndpoints; k++) {
        printf(" endpoint %d, addr:%#x", k,
               interface.endpoint[k].bEndpointAddress);
      }
      printf("\n");
    }
  }
}

int run(void) {
  // std::thread *usb_thread = new std::thread();

  int rc;

#if defined(PLATFORM_POSIX)
  struct sigaction sigact;

  sigact.sa_handler = sig_hdlr;
  sigemptyset(&sigact.sa_mask);
  sigact.sa_flags = 0;
  (void)sigaction(SIGINT, &sigact, NULL);
#else
  (void)signal(SIGINT, sig_hdlr);
#endif

  rc = libusb_init_context(/*ctx=*/NULL, /*options=*/NULL, /*num_options=*/0);
  if (rc < 0) {
    fprintf(stderr, "Error initializing libusb: %s\n", libusb_error_name(rc));
    exit(1);
  }

  devh = libusb_open_device_with_vid_pid(NULL, 0x2D40, 0x00B6 /*0x901D*/);
  if (!devh) {
    fprintf(stderr, "Error finding USB device\n");
  }
  printDev();

  rc = libusb_claim_interface(devh, 1);
  if (rc < 0) {
    fprintf(stderr, "Error claiming interface: %s\n", libusb_error_name(rc));
  }

  const int frame_duration = 2;
  
  std::shared_ptr<std::thread> write_thread =
      std::make_shared<std::thread>([frame_duration]() {
    auto last_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    while (true) {
      benchmark_out(0x02);
      auto current_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
      auto delta = current_ms - last_ms;
      last_ms = current_ms;
      if (delta < frame_duration) {
        Sleep(frame_duration - delta);
      }
    }
  });

   std::shared_ptr<std::thread> read_thread =
      std::make_shared<std::thread>([frame_duration]() {
        auto last_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
        while (true) {
          benchmark_in(0x83);
          auto current_ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
          auto delta = current_ms - last_ms;
          last_ms = current_ms;
          if (delta < frame_duration) {
            Sleep(frame_duration - delta);
          }
        }
      });
  //benchmark_in(0x83);

  while (!do_exit) {
    
    rc = libusb_handle_events(NULL);

    // unsigned char data[256] = {0};
    //        int read_length = 0;
    //        int ret =
    //            libusb_bulk_transfer(devh, 0x01, data, 256, &read_length, 0);
    //        if (0 == ret) {
    //          printf("read ret %d, length %d, %s\n", ret, read_length, data);
    //        }
    if (rc != LIBUSB_SUCCESS) break;
  }

  /* Measurement has already been done by the signal handler. */

  libusb_release_interface(devh, 0);
out:
  if (devh) libusb_close(devh);
  libusb_exit(NULL);
  return rc;
}


int main(void) {
  std::shared_ptr <std::thread> local_thread =
      std::make_shared<std::thread>([]() { run(); });
  while (true) {
  }
  return 0;
}