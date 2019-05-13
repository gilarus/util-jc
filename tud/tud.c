/*
 * TUD (The Ugly Duckling) - Serial Port Debugging tool for Linux.
 * Copyright (C) 2019 Gimcuan Hui
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <ctype.h>
#include <malloc.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

#define print(...) do { \
		if (FLAGS & OPT_V) \
			printf(__VA_ARGS__); \
	} while (0)

#define OPT_B 0x001
#define OPT_C 0x002
#define OPT_H 0x004
#define OPT_O 0x008
#define OPT_R 0x010
#define OPT_T 0x020
#define OPT_V 0x040
#define OPT_W 0x080
#define OPT_X 0x100

#define EXIT_CODE_WAIT  0
#define EXIT_CODE_USAGE 1
#define EXIT_CODE_TERMI 2
#define EXIT_CODE_ERROR 3

#define print_termios_setting(p_opt) do { \
	print("Setting baud: %d\n", baud); \
	print("c_cflag\t%d\n", (p_opt)->c_cflag); \
	print("c_lflag\t%d\n", (p_opt)->c_lflag); \
	print("c_iflag\t%d\n", (p_opt)->c_iflag); \
	print("c_oflag\t%d\n", (p_opt)->c_oflag); \
	print("c_cc\t%d\n", (p_opt)->c_cc); \
	print("c_ispe\t%o\n", (p_opt)->c_ispeed); \
	print("c_ospe\t%o\n", (p_opt)->c_ospeed); \
} while (0)

typedef enum {
	ERR_OPEN = 0x0,
	ERR_NOTGT,
	ERR_BAUD,
	ERR_PTHREAD,
	ERR_WRITE,
	ERR_COUNT,
	ERR_INPUT,
	ERR_UNKNOWN,
	ERR_REQ_ARG,
	ERR_SIG,
} ERR_CODE;

static int verbose = 0;
static int fd = 0;
static char *dev = NULL;
static char *ptx = NULL;
static int FLAGS = 0;
static unsigned int timing_v;
static int count = -1;
static int baud = 115200;
static int c;
static int len;
static int len_b;
static int n;
static pthread_t thread_id = -1;

static const char *err_posters[] = {
	/* ERR_OPEN  */   "open failed",
	/* ERR_NOTGT */   "no target specified",
	/* ERR_BAUD  */   "setting baud failed",
	/* ERR_PTHREAD */ "pthread error",
	/* ERR_WRITE */   "write failed",
	/* ERR_COUNT */   "illegal count",
	/* ERR_INPUT */   "illegal input",
	/* ERR_UNKNOWN */ "unknown err",
	/* ERR_REQ_ARG */ "argument required",
	/* ERR_SIG */     "signal error",
};

static void exit_threads(void);
static void join_threads(void);

void close_port(int fd);

static void usage(void)
{
	puts("Usage: tud [OPTION]...");
	puts("[OPTIONS]");
	printf("  -b\tsetting baud rate, default 115200\n");
	printf("  -h\tdisplay this message\n");
	printf("  -o\topen target serial port, eg. /dev/ttyUSB0\n");
	printf("  -w\twrite target serial port\n");
	printf("  -r\tread target serial port\n");
	printf("  -v\tverbose mode\n");
	printf("  -c\tspecify sending repeat counts\n");
	printf("  -t\tspecify sending period\n");
	printf("  -x\tparse in hex mode\n");
}

static void EXIT(int exit_code)
{
	if (exit_code == EXIT_CODE_USAGE) {
		usage();
	} else if (exit_code == EXIT_CODE_TERMI) {
		print("Shutting down..\n");
	} else if (exit_code == EXIT_CODE_ERROR) {
	} else if (exit_code == EXIT_CODE_WAIT) {
		join_threads();
	}
real_exit:
	close_port(fd);
	exit_threads();
	exit(0);
}

static void error_handler(ERR_CODE err)
{
	fprintf(stderr, "%s.\n", err_posters[err]);
	EXIT(EXIT_CODE_ERROR);
}

void close_port(int fd)
{
	if (fd) {
		print("Closing port %s.\n", dev);
		close(fd);
	}
}

int open_port(char *dev)
{
	int fd;

	print("Opening port %s\n", dev);
	fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd == -1)
		error_handler(ERR_OPEN);
	else
		fcntl(fd, F_SETFL, 0); //0: blocking
	print(" ok\n");
	return (fd);
}

int isallhexdigit(char *c, int len)
{
	int i;
	for (i = 0; i < len; i++)
		if (!isxdigit(*(c + i)))
			return 0;
	return 1;
}

static void *thread_start(void *arg)
{
	int n;
	char c;
	int fd;

	fd = *(int *)arg;
	for (;;) {
		n = read(fd, &c, 1);
		if (n > 0)
			printf("%c", c);
	}
	return NULL;
}

int start_rdp(int fd)
{
	int s;
	int *pfd;
	pthread_attr_t attr;

	pfd = malloc(sizeof(int));
	*pfd = fd;
	s = pthread_attr_init(&attr);
	if (s != 0)
		goto err;
	s = pthread_create(&thread_id, &attr, &thread_start, pfd);
	if (s != 0)
		goto err;
	s = pthread_attr_destroy(&attr);
	if (s != 0)
		goto err;
	return thread_id;
err:
	return -1;
}

static void exit_threads(void)
{
	if (thread_id == -1) return;
	pthread_cancel(thread_id);
}

static void join_threads(void)
{
	int s;
	void *res;
	if (thread_id == -1) return;
	s = pthread_join(thread_id, &res);
	if (s != 0)
		error_handler(ERR_PTHREAD);
	free(res);
}

/* 8N1 */
void set_no_parity(struct termios *options)
{
	options->c_cflag &= ~PARENB;
	options->c_cflag &= ~CSTOPB;
	options->c_cflag &= ~CSIZE;
	options->c_cflag |= CS8;
}
/* 7E1 */
void set_even_parity(struct termios *options)
{
	options->c_cflag |= PARENB;
	options->c_cflag &= ~PARODD;
	options->c_cflag &= ~CSTOPB;
	options->c_cflag &= ~CSIZE;
	options->c_cflag |= CS7;
}
/* 7O1 */
void set_odd_parity(struct termios *options)
{
	options->c_cflag |= PARENB;
	options->c_cflag |= PARODD;
	options->c_cflag &= ~CSTOPB;
	options->c_cflag &= ~CSIZE;
	options->c_cflag |= CS7;
}
/* 7S1 */
void set_space_parity(struct termios *options)
{
	set_no_parity(options);
}
/* Hardware flow control */
void set_hrdflow_ctl(struct termios *options, int en)
{
	if (en)
		options->c_cflag |= CRTSCTS;
	else
		options->c_cflag &= ~CRTSCTS;
}

void reset_termios_opts(struct termios *options)
{
	options->c_cflag  &= 0;
	options->c_lflag  &= 0;
	options->c_iflag  &= 0;
	options->c_oflag  &= 0;
	options->c_ispeed &= 0;
	options->c_ospeed &= 0;
}

int setting_baud(int fd, int baud)
{
	struct termios options;
	speed_t speed;
	tcgetattr(fd, &options);
	print_termios_setting(&options);

	reset_termios_opts(&options);

	switch (baud) {
	case 115200:
		speed = B115200;
	break;
	case 57600:
		speed = B57600;
	break;
	case 9600:
		speed = B9600;
	break;
	default:
		speed = B115200;
	break;
	}

	cfsetispeed(&options, speed);
	cfsetospeed(&options, speed);

	set_no_parity(&options);
	set_hrdflow_ctl(&options, 0);

	tcsetattr(fd, TCSANOW, &options);
	print_termios_setting(&options);
	print(" ok\n");
	return 0;
}

static void opt_open(char *dev)
{
	fd = open_port(dev);
	if (fd == -1)
		error_handler(ERR_OPEN);
}

static void opt_baud(int baud)
{
	if (!fd) {
		error_handler(ERR_NOTGT);
	} else {
		if (setting_baud(fd, baud))
			error_handler(ERR_BAUD);
	}
}

static void opt_read(void)
{
	if (!fd)
		error_handler(ERR_NOTGT);
	if (-1 == start_rdp(fd))
		error_handler(ERR_PTHREAD);
}

static void opt_write(char *ptx, int len)
{
	char *ptx_b = NULL;

	if (!fd)
		error_handler(ERR_NOTGT);
	len = strlen(ptx);
	ptx_b = malloc(len);
	if (!ptx_b)
		error_handler(ERR_WRITE);
	if (FLAGS & OPT_X) {
		if (!isallhexdigit(ptx, len))
			error_handler(ERR_INPUT);
		if (!sscanf(ptx, "%x", ptx_b))
			error_handler(ERR_UNKNOWN);
		len_b = (len + 1) >> 1;
	} else {
		len_b = len;
		memcpy(ptx_b, ptx, len_b);
	}
tx:
	print("Writing buffer(%d)\n", len_b);
	n = write(fd, ptx_b, len_b);
	if (n < 0)
		error_handler(ERR_WRITE);
	else
		print(" ok\n %d bytes wrote.\n", n);
	if (count == ERANGE)
		error_handler(ERR_COUNT);
	if (count > 0) {
		count--;
		if (count > 0) {
			if (FLAGS & OPT_T)
				sleep(timing_v);
			else
				sleep(1);
			goto tx;
		}
	} else if (count < 0) {
		if (FLAGS & OPT_T) {
			sleep(timing_v);
			goto tx;
		}
	}
	free(ptx_b);
}

static void opt_parser(void)
{
	if (FLAGS & OPT_H)
		usage();
	if (FLAGS & OPT_O)
		opt_open(dev);
	if (FLAGS & OPT_B)
		opt_baud(baud);
	else
		opt_baud(115200);
	if (FLAGS & OPT_R)
		opt_read();
	if (FLAGS & OPT_W)
		opt_write(ptx, len);
	EXIT(EXIT_CODE_WAIT);
}

static void sigs_handler(int signo, siginfo_t *info, void *extra)
{
	if (signo == SIGTERM || signo == SIGINT) {
		EXIT(EXIT_CODE_TERMI);
	}
}

void subscribe_signals(void)
{
	struct sigaction action;

	action.sa_flags = SA_SIGINFO;
	action.sa_sigaction = sigs_handler;
	if (sigaction(SIGTERM, &action, NULL) == -1)
		error_handler(ERR_SIG);
	if (sigaction(SIGINT, &action, NULL) == -1)
		error_handler(ERR_SIG);
}
int main(int argc, char **argv)
{
	int index;

	subscribe_signals();
	opterr = 0;

	while ((c = getopt(argc, argv, "b:c:ho:rt:vw:x")) != -1) {
		switch (c)
		{
			case 'h':
				FLAGS |= OPT_H;
				break;
			case 'o':
				dev = optarg;
				FLAGS |= OPT_O;
				break;
			case 'r':
				FLAGS |= OPT_R;
				break;
			case 'w':
				FLAGS |= OPT_W;
				ptx = optarg;
				break;
			case 'x':
				FLAGS |= OPT_X;
				break;
			case '?':
				if (optopt == 'o' || optopt == 'w' || optopt == 't')
					fprintf(stderr, "Option -%c requires an argument.\n", optopt);
				else if (isprint(optopt))
					fprintf(stderr, "Unknown option '-%c'.\n", optopt);
				else
					fprintf(stderr, "Unknown option character '\\x%x'.\n", optopt);
				EXIT(EXIT_CODE_USAGE);
			case 'v':
				FLAGS |= OPT_V;
				break;
			case 'c':
				FLAGS |= OPT_C;
				count = strtoul(optarg, NULL, 10);
				break;
			case 't':
				FLAGS |= OPT_T;
				timing_v = strtoul(optarg, NULL, 10);
				break;
			case 'b':
				FLAGS |= OPT_B;
				baud = strtoul(optarg, NULL, 10);
				break;
			default:
				abort();
		}
	}
	opt_parser();
	for (index = optind; index < argc; index++)
		print("Non-option argument %s\n", argv[index]);
}
