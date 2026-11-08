#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

typedef struct command{
	char *arg;
	struct command *next;
}command;

char *home_path, cwd[1024];

int child_run(char **input){
	return 1;
    // build in input check + execute
    if (strcmp(input[0], "cd") == 0)
    {
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
		// printf("%s\n", input[i]);
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

command* process_wildcard(command *cur, int *total){
	int len = strlen(cur->arg);
	int l, have = 0;
	for(l=len-1;l>=0;--l){
		if(cur->arg[l] == '/') break;
		if(cur->arg[l] == '*') have=1;
	}
	if(!have) return cur;

	DIR *dir;
	if(l > -1){
		if(cur->arg[0] == '/'){
			cur->arg[l] = '\0';
			dir = opendir(cur->arg);
		}
		else{
			char *full_path = malloc(sizeof(char)*(sizeof(cwd)+l));
			strcpy(full_path, cwd);
			strcat(full_path, "/");
			strncat(full_path, cur->arg, l);
			dir = opendir(full_path);
			free(full_path);
		}
	}
	else dir = opendir(cwd);
	if(!dir) return cur;

	struct dirent *en;
	char *token = cur->arg;
	int cnt = 0;
	while((en = readdir(dir))){
		if(en->d_name[0] == '.') continue;

		int name_len = strlen(en->d_name);
		int **dp = malloc(sizeof(int*)*(name_len+1));
		dp[0] = calloc(len-l, sizeof(int));
		dp[0][0] = 1;
		for(int i=1;i<len-l;++i){
			if(token[l+i] == '*') dp[0][i] = 1;
			else break;
		}
		for(int i=1;i<=name_len;++i){
			dp[i] = calloc(len-l, sizeof(int));
			for(int j=1;j<len-l;++j){
				if(token[l+j] == '*')
					dp[i][j] = dp[i-1][j] | dp[i-1][j-1];
				else if(token[l+j] == en->d_name[i-1])
					dp[i][j] = dp[i-1][j-1];
			}
		}
		if(dp[name_len][len-l-1]){
			char *new = malloc(sizeof(char)*(l+1+strlen(en->d_name)+1));
			if(l > -1) strncpy(new, token, l+1);
			strcpy(new+l+1, en->d_name);
			if(!cnt)
				cur->arg = new;
			else{
				command *nxt = cur->next;
				cur->next = malloc(sizeof(command));
				cur = cur->next;
				cur->next = nxt;
				cur->arg = new;
			}
			printf("%s\n", cur->arg);
			cnt++;
		}
		for(int i=0;i<=name_len;++i)
			free(dp[i]);
		free(dp);
	}
	if(cnt > 0){
		*total += cnt-1;
		free(token);
	}
	closedir(dir);
	return cur;
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
				if(cnt > 0) tmp = process_wildcard(tmp, &cnt);
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
