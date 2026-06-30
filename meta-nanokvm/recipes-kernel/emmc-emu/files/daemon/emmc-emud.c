// SPDX-License-Identifier: GPL-2.0
/*
 * emmc-emud - userspace backing-store daemon for the NanoKVM eMMC emulator.
 *
 * The kernel module RAM-backs the emulated card and exposes it at
 * /dev/emmc-emu0 (read/write/mmap of the raw image, plus an info ioctl). This
 * daemon gives that backing store persistence and a management surface:
 *
 *   - load:  copy a disk image file into the device before the host boots.
 *   - sync:  periodically (or on demand) write the device's contents back to
 *            that file, so writes the Raspberry Pi makes to the emulated eMMC
 *            (e.g. its U-Boot EFI variable store, ubootefi.var) survive.
 *   - info:  print live bus statistics from the module.
 *
 * It is intentionally a thin, dependency-free C program; the actual EFI
 * variable parsing/editing for "instant board control" layers on top of the
 * synced image file with a separate tool.
 *
 * Usage:
 *   emmc-emud --dev /dev/emmc-emu0 --image /var/lib/nanokvm/emmc.img \
 *             [--load] [--interval 5] [--once] [--info]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "emmc_uapi.h"

#define BLK 512

static volatile sig_atomic_t g_stop;
static void on_signal(int s) { (void)s; g_stop = 1; }

static int read_info(int fd, struct emmc_info *info)
{
	return ioctl(fd, EMMC_IOC_GET_INFO, info);
}

static void print_info(const struct emmc_info *i)
{
	static const char *states[] = {
		"idle", "ready", "ident", "stby",
		"tran", "data", "rcv", "prg", "dis"
	};

	printf("capacity   : %llu bytes (%llu blocks)\n",
	       (unsigned long long)i->capacity_bytes,
	       (unsigned long long)i->capacity_blocks);
	printf("spec_vers  : %u (%s)\n", i->spec_vers,
	       i->spec_vers < 4 ? "legacy, no EXT_CSD" : "v4 + EXT_CSD");
	printf("addressing : %s\n", i->high_capacity ? "sector" : "byte");
	printf("rca        : 0x%04x   state: %s\n", i->rca,
	       i->state < 9 ? states[i->state] : "?");
	printf("last cmd   : CMD%-2u  arg=0x%08x\n", i->last_cmd, i->last_arg);
	printf("serviced   : %u commands, %u CRC errors\n",
	       i->cmd_count, i->crc_errors);
}

/* Copy the whole device <-> file. dir: 0 = file->dev (load), 1 = dev->file. */
static int transfer(int devfd, const char *path, int dir, unsigned long long cap)
{
	int filefd, ret = 0;
	unsigned long long done = 0;
	char buf[64 * 1024];

	filefd = open(path, dir ? (O_WRONLY | O_CREAT) : O_RDONLY, 0644);
	if (filefd < 0) {
		fprintf(stderr, "open(%s): %s\n", path, strerror(errno));
		return -1;
	}
	if (lseek(devfd, 0, SEEK_SET) < 0)
		{ ret = -1; goto out; }

	while (done < cap && !g_stop) {
		ssize_t n, w;
		size_t chunk = sizeof(buf);

		if (cap - done < chunk)
			chunk = cap - done;

		if (dir == 0) {				/* file -> dev */
			n = read(filefd, buf, chunk);
			if (n <= 0)
				break;			/* short image is fine */
			w = write(devfd, buf, n);
		} else {				/* dev -> file */
			n = read(devfd, buf, chunk);
			if (n <= 0)
				break;
			w = write(filefd, buf, n);
		}
		if (w != n) {
			fprintf(stderr, "transfer short write: %s\n",
				strerror(errno));
			ret = -1;
			break;
		}
		done += n;
	}
out:
	close(filefd);
	return ret;
}

static void usage(const char *p)
{
	fprintf(stderr,
		"usage: %s [--dev DEV] [--image FILE] [--load] [--once]\n"
		"          [--interval SECS] [--info]\n", p);
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/emmc-emu0";
	const char *image = NULL;
	int do_load = 0, once = 0, info_only = 0, interval = 5;
	struct emmc_info info;
	int fd, i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--dev") && i + 1 < argc)
			dev = argv[++i];
		else if (!strcmp(argv[i], "--image") && i + 1 < argc)
			image = argv[++i];
		else if (!strcmp(argv[i], "--load"))
			do_load = 1;
		else if (!strcmp(argv[i], "--once"))
			once = 1;
		else if (!strcmp(argv[i], "--info"))
			info_only = 1;
		else if (!strcmp(argv[i], "--interval") && i + 1 < argc)
			interval = atoi(argv[++i]);
		else { usage(argv[0]); return 2; }
	}

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open(%s): %s\n", dev, strerror(errno));
		return 1;
	}
	if (read_info(fd, &info)) {
		fprintf(stderr, "ioctl GET_INFO: %s\n", strerror(errno));
		close(fd);
		return 1;
	}

	if (info_only) {
		print_info(&info);
		close(fd);
		return 0;
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	if (do_load && image) {
		printf("loading %s into %s ...\n", image, dev);
		if (transfer(fd, image, 0, info.capacity_bytes))
			fprintf(stderr, "load failed\n");
	}

	if (!image) {				/* nothing to persist */
		print_info(&info);
		close(fd);
		return 0;
	}

	printf("emmc-emud: syncing %s every %ds (Ctrl-C to stop)\n",
	       image, interval);
	do {
		int s;

		for (s = 0; s < interval && !g_stop; s++)
			sleep(1);
		if (g_stop)
			break;
		if (transfer(fd, image, 1, info.capacity_bytes) == 0) {
			read_info(fd, &info);
			printf("synced; %u cmds, last CMD%u\n",
			       info.cmd_count, info.last_cmd);
		}
	} while (!once && !g_stop);

	/* Final flush on exit. */
	transfer(fd, image, 1, info.capacity_bytes);
	close(fd);
	return 0;
}
