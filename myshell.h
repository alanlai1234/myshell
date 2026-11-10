#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

typedef struct command{
	char *arg;
	int len;
	struct command *next;
}command;

typedef struct pipes{
	char **input;
	int cnt;
	struct pipes *next;
}pipes;

char *home_path, cwd[1024];
pipes *pipe_start, *pipe_end;
