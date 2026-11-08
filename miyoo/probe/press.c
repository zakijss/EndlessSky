// press: inject Miyoo button events into /dev/input/event0, as a real press would arrive.
//   press 28        tap Start (held 80 ms)       press 103:400   hold Up for 400 ms
//   press +97 57 -97   hold Select, tap A, release Select    press w500   wait 500 ms
// Codes: Up 103 Down 108 Left 105 Right 106 A 57 B 29 X 42 Y 56 L1 18 L2 15 R1 20 R2 14
//        Select 97 Start 28 Menu 1.
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static void Key(int fd, int code, int down)
{
	struct input_event e[2];
	memset(e, 0, sizeof(e));
	gettimeofday(&e[0].time, NULL);
	e[1].time = e[0].time;
	e[0].type = EV_KEY;
	e[0].code = code;
	e[0].value = down;
	e[1].type = EV_SYN;
	e[1].code = SYN_REPORT;
	if(write(fd, e, sizeof(e)) != sizeof(e))
		perror("write");
}

int main(int argc, char **argv)
{
	int fd = open("/dev/input/event0", O_WRONLY);
	if(fd < 0)
	{
		perror("open");
		return 1;
	}
	for(int i = 1; i < argc; ++i)
	{
		const char *arg = argv[i];
		if(arg[0] == 'w')
			usleep(atoi(arg + 1) * 1000);
		else if(arg[0] == '+' || arg[0] == '-')
			Key(fd, atoi(arg + 1), arg[0] == '+');
		else
		{
			const char *colon = strchr(arg, ':');
			Key(fd, atoi(arg), 1);
			usleep((colon ? atoi(colon + 1) : 80) * 1000);
			Key(fd, atoi(arg), 0);
			usleep(150 * 1000);
		}
	}
	close(fd);
	return 0;
}
