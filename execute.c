#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

typedef struct command{
	char *arg;
	struct command *next;
}command;

int child_run(char **input){
    // build in input check + execute
    if (strcmp(input[0], "cd") == 0)
    {
        char cwd[1024];
        getcwd(cwd, sizeof(cwd));
        printf("before cd: %s", cwd);

        if (input[1] == NULL)
        {
            if (chdir(getenv("HOME")) == -1)
            {
                perror("cd");
            }
        }
        else if (input[2] != NULL)
        {
            fprintf(stderr, "cd: too many arguments\n");
        }
        else
        {
            // 剛好一個 argument
            if (chdir(input[1]) == -1)
            {
                perror("cd");
            }
        }
    }
    else
    {
        //====================================================================
        // input route check
        int slash = 0;
        int length = strlen(input[0]);
        for (int x = 0; x < length; x++)
        {
            if (input[0][x] == '/')
            {
                printf("Found slash\n");
                slash = 1;
                break;
            }
        }

        if (slash == 0)
        {
            printf("Slash not found\n");
        }

        if (slash == 1)
        {
            if (access(input[0], X_OK) == -1)
            {
                perror(input[0]);
            }else{
                pid_t pid = fork();
                if (pid == -1)
                {
                    perror("fork");
                }
                else if (pid == 0)
                {
                    execv(input[0], input);

                     perror("execv");
                    _exit(1);
                }
                else
                {
                    int status;
                    waitpid(pid, &status, 0);
                }
            }
        }
        //====================================================================
        // input command check
        const char *directories[] = {
            "/usr/local/bin",
            "/usr/bin",
            "/bin"};
        int found = 0;
        char path[1024];
        for (int x = 0; x < 3; x++)
        {
            snprintf(path, sizeof(path), "%s/%s", directories[x], input[0]);

            if (access(path, X_OK) == 0)
            {
                found = 1;
                printf("Found command\n");
                break;
            }
        }
        if (found == 0)
        {
            printf("command not found\n");
        }
        if (found == 1)
        {
            if (access(path, X_OK) == -1)
            {
                perror(path);
            }else{
                pid_t pid = fork();
                if (pid == -1)
                {
                    perror("fork");
                }
                else if (pid == 0)
                {
                    execv(path, (path,input));

                     perror("execv");
                    _exit(1);
                }
                else
                {
                    int status;
                    waitpid(pid, &status, 0);
                }
            }
        } 
    }
	return 0;
}

int run(command *start, int cnt, int *prev_pipe, int is_pipe){
	char **input = malloc(sizeof(char*)*(cnt+1));
	for(int i=0;i<cnt;++i){
		input[i] = start->arg;
		start = start->next;
	}
	input[cnt] = NULL;
	int p[2];
	if(is_pipe){
		if(pipe(p) == -1) return 0;
	}

	int c = fork();
	if(c == -1) return 0;
	if(c == 0){
		if(*prev_pipe != -1){
			if(dup2(*prev_pipe, STDIN_FILENO) == -1) abort();
			close(*prev_pipe);
		}
		if(is_pipe){
			if(dup2(p[1], STDOUT_FILENO) == -1) abort();
			close(p[1]);
			*prev_pipe = p[0];
		}
		child_run(input);
		abort();
	}
	free(input);
	return 1;
}

int execute(command *start){
	int prev_pipe = -1;
	int cmd_cnt = 0;
	while(start){
		command *tmp = start;
		int cnt = 0;
		cmd_cnt++;
		while(tmp){
			if(!strcmp(tmp->arg, "|")){
				if(!run(start, cnt, &prev_pipe, 1)) return 0;
				start = tmp->next;
				break;
			}
			else{
				tmp = tmp->next;
				cnt++;
			}
		}
		if(!tmp){
			if(!run(start, cnt, &prev_pipe, 0)) return 0;
			start = tmp;
		}
	}
	while(cmd_cnt--) wait(NULL);
	return 1;
}
