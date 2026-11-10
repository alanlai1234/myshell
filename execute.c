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
int arg_cnt = 0;

static const char *find_executable(const char *name, char *path, size_t size)
{
    if (strchr(name, '/') != NULL) {
        if (access(name, X_OK) == 0) {
            return name;
        }
        return NULL;
    }

    const char *directories[] = {"/usr/local/bin", "/usr/bin", "/bin"};

    for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); i++) {
        int n = snprintf(path, size, "%s/%s", directories[i], name);
        if (n >= 0 && (size_t)n < size && access(path, X_OK) == 0) {
            return path;
        }
    }

    return NULL;
}

int child_run(char **input, int len)
{
    if (len == 0) {
        return 1;
    }

    // [隊友原本的分支] exit 和 cd
    if (strcmp(input[0], "exit") == 0) {
        return 0;
    }

    if (strcmp(input[0], "cd") == 0) {
        if (len > 2) return 1;
        if (input[1] == NULL) {
            if (chdir(getenv("HOME")) == -1) return 1;
        } else if (chdir(input[1]) == -1) {
            return 1;
        }
        return 0;
    }

    // [我們的 launcher] pwd、which 和外部指令
    char path[1024];

    if (strcmp(input[0], "pwd") == 0) {
        if (len != 1) {
            fprintf(stderr, "pwd: too many arguments\n");
            return 1;
        }
        return dprintf(STDOUT_FILENO, "%s\n", cwd) < 0 ? 1 : 0;
    }

    if (strcmp(input[0], "which") == 0) {
        if (len < 2) {
            fprintf(stderr, "which: missing argument\n");
            return 1;
        }

        int failed = 0;
        for (int i = 1; i < len; i++) {
            const char *found = find_executable(input[i], path, sizeof(path));
            if (found == NULL) {
                fprintf(stderr, "which: no executable found for %s\n", input[i]);
                failed = 1;
            } else if (dprintf(STDOUT_FILENO, "%s\n", found) < 0) {
                failed = 1;
            }
        }
        return failed;
    }

    const char *found = find_executable(input[0], path, sizeof(path));
    if (found == NULL) {
        if (strchr(input[0], '/') != NULL) {
            perror(input[0]);
        } else {
            fprintf(stderr, "%s: command not found\n", input[0]);
        }
        return 1;
    }

    execv(found, input);
    perror(found);
    return 1;
}

void cmd_cd(pipes *cur){
	if(cur->cnt > 2) printf("cd: too many arguments\n");
	else if (cur->input[1] == NULL){
		if (chdir(getenv("HOME")) == -1) perror("cd");
	}
	else if (chdir(cur->input[1]) == -1) perror("cd");
	if(getcwd(cwd, sizeof(cwd)) == NULL) printf("cwd too small\n");
}

int run(pipes *cur, int *prev_pipe, int is_pipe){
	int p[2];
	if(is_pipe) if(pipe(p) == -1) return -1;

	int c = fork();
	if(c == -1) return -1;
	if(c == 0){
		if(*prev_pipe != -1){
			if(dup2(*prev_pipe, STDIN_FILENO) == -1) _exit(1);
			close(*prev_pipe);
		}
		if(is_pipe){
			if(dup2(p[1], STDOUT_FILENO) == -1) _exit(1);
			close(p[1]);
			close(p[0]);
		}

		//detect redirection
		int redirect = 0; //0:none, 1:<, 2:>, 3:>>
		int end = -1;
		for(int i=0;i<cur->cnt;++i){
			if(!strcmp(cur->input[i], "<")){
				if(end == -1) end = i;
				if(redirect != 0){
					fprintf(stderr, "myshell: parse error\n");
					_exit(1);
				}
				redirect = 1;
			}
			else if(!strcmp(cur->input[i], ">")){
				if(end == -1) end = i;
				if(redirect != 0){
					fprintf(stderr, "myshell: parse error\n");
					_exit(1);
				}
				redirect = 2;
			}
			else if(!strcmp(cur->input[i], ">>")){
				if(end == -1) end = i;
				if(redirect != 0){
					fprintf(stderr, "myshell: parse error\n");
					_exit(1);
				}
				redirect = 3;
			}
			else if(redirect == 1){
				int fd = open(cur->input[i], O_RDONLY);
				if(fd < 0){
					perror(cur->input[i]);
					_exit(1);
				}
				if(dup2(fd, STDIN_FILENO) == -1) _exit(1);
				redirect = 0;
			}
			else if(redirect == 2){
				int fd = open(cur->input[i], O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP);
				if(fd < 0){
					perror(cur->input[i]);
					_exit(1);
				}
				if(dup2(fd, STDOUT_FILENO) == -1) _exit(1);
				redirect = 0;
			}
			else if(redirect == 3){
				int fd = open(cur->input[i], O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR|S_IRGRP);
				if(fd < 0){
					perror(cur->input[i]);
					_exit(1);
				}
				if(dup2(fd, STDOUT_FILENO) == -1) _exit(1);
				redirect = 0;
			}
		}
		if(redirect != 0){
			fprintf(stderr, "myshell: parse error\n");
			_exit(1);
		}

		if(end != -1){
			cur->input[end] = NULL;
			cur->cnt = end;
		}
		int ret = child_run(cur->input, cur->cnt);
		_exit(ret);
	}

	if(!strcmp(cur->input[0], "cd") && !is_pipe){
		cmd_cd(cur);
	}

	if(is_pipe){
		*prev_pipe = p[0];
		close(p[1]);
	}
	return c;
}

command* process_wildcard(command *cur){
	int l, have = 0, len = cur->len;
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
			char *new = malloc(sizeof(char)*(l+1+name_len+1));
			if(l > -1) strncpy(new, token, l+1);
			strcpy(new+l+1, en->d_name);
			if(!cnt){
				cur->arg = new;
				cur->len = l+1+name_len;
			}
			else{
				if(cur->next == NULL) cur->next = malloc(sizeof(command));
				cur = cur->next;
				if(cur->arg) free(cur->arg); // notice if this creates bug
				cur->arg = new;
				cur->len = l+1+name_len;
			}
			cnt++;
		}
		for(int i=0;i<=name_len;++i)
			free(dp[i]);
		free(dp);
	}
	if(cnt > 0){
		arg_cnt += cnt-1;
		free(token);
	}
	closedir(dir);
	return cur;
}

void add_pipe(command *start){
	pipe_end->next = malloc(sizeof(pipes));
	pipe_end = pipe_end->next;
	pipe_end->input = malloc(sizeof(char*)*(arg_cnt+1));
	pipe_end->cnt = arg_cnt;
	for(int i=0;i<arg_cnt;++i){
		pipe_end->input[i] = start->arg;
		start = start->next;
	}
	pipe_end->input[arg_cnt] = NULL;
	arg_cnt = 0;
}

int execute(){
	int prev_pipe = -1;
	int cmd_cnt = 0;
	int last_pid, exits = 0;
	pipes *start = pipe_start->next;
	while(start){
		if(!strcmp(start->input[0], "exit")) exits = 1;

		int rt = run(start, &prev_pipe, start->next!=NULL);
		if(rt == -1) perror("failed to create child process or pipe\n");
		else cmd_cnt++, last_pid = rt;

		start = start->next;
	}

	int last_status;
	//TODO: handle failed wait
	waitpid(last_pid, &last_status, 0);
	cmd_cnt--;
	while(cmd_cnt--) wait(NULL);
	//0:fail, 1:success, 2:fail+exit, 3:success+exit
	last_status = WEXITSTATUS(last_status);
	if(exits) last_status += 2;
	return last_status;
}
