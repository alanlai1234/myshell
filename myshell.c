#include <fcntl.h>
#include "execute.c"
#define BUFFER_SIZE 10

char buf[BUFFER_SIZE];
char *word;
int home_path_len;
int interactive = 0;

command *cmd_start;

void print_prompt(){
	if(!strncmp(cwd, home_path, home_path_len))
		printf("~%s$ ", cwd+home_path_len);
	else
		printf("%s$ ", cwd);
	fflush(stdout);
}

command* add_arg(char *arg, int len, command *end){
	if(len == 0) return end;
	if(end->next == NULL) end->next = malloc(sizeof(command));
	end = end->next;
	if(end->arg) free(end->arg);
	end->arg = malloc(sizeof(char)*(len+1));
	arg[len] = '\0';
	strcpy(end->arg, arg);
	return end;
}

void cleanup(){
	free(word);
	while(cmd_start){
		command *tmp = cmd_start;
		cmd_start = cmd_start->next;
		free(tmp->arg);
		free(tmp);
	}
	if(interactive) printf("Exiting myshell\n");
}

int main(int argc, char **argv){
	atexit(cleanup);
	if(argc>1){
		int fd = open(argv[1], O_RDONLY);
		if(fd < 0){
			perror(argv[1]);
			exit(EXIT_FAILURE);
		}
		dup2(fd, 0);
	}
	else if(isatty(0)){
		interactive = 1;
		printf("Welcome to my shell!\n");
	}

	//setup
	home_path = getenv("HOME");
	home_path_len = strlen(home_path);
	if(getcwd(cwd, sizeof(cwd)) == NULL)
		printf("cwd too small\n");
	int word_max = 50, word_cur = 0;
	word = malloc(word_max+1);
	size_t rd;
	cmd_start = malloc(sizeof(command));
	command *cmd_end = cmd_start;
	if(interactive) print_prompt();

	//start
	while((rd = read(0, buf, BUFFER_SIZE)) > 0){
		for(int i=0;i<rd;++i){
			if(buf[i] == '\n' || buf[i]=='\0'){
				if(word_cur > 0){
					cmd_end = add_arg(word, word_cur, cmd_end);
					word_cur = 0;
				}
				int ret = execute(cmd_start->next, cmd_end->next);
				if(ret == 1 || ret == 3) printf("failed to execute\n");
				if(ret > 1){
					exit(EXIT_SUCCESS);
				}

				cmd_end = cmd_start;
				if(interactive) print_prompt();
			}
			else if(buf[i] == ' '){
				cmd_end = add_arg(word, word_cur, cmd_end);
				word_cur = 0;
			}
			else if(buf[i] == '<' || buf[i] == '|'){
				cmd_end = add_arg(word, word_cur, cmd_end);
				word[0] = buf[i];
				cmd_end = add_arg(word, 1, cmd_end);
				word_cur = 0;
			}
			else if(buf[i] == '>'){
				if(word_cur > 0){
					if(word[word_cur-1] != '>'){
						cmd_end = add_arg(word, word_cur, cmd_end);
						word_cur = 0;
						word[word_cur++] = '>';
					}
					else{
						word[word_cur++] = '>';
						cmd_end = add_arg(word, word_cur, cmd_end);
						word_cur = 0;
					}
				}
				else word[word_cur++] = '>';
			}
			else{
				if(word_cur > 0) if(word[word_cur-1] == '>'){
					cmd_end = add_arg(word, word_cur, cmd_end);
					word_cur = 0;
				}
				word[word_cur++] = buf[i];
			}

			if(word_cur == word_max){
				word_max += 5;
				char *tmp = realloc(word, word_max+1);
				if(tmp == NULL) exit(1);
				word = tmp;
			}

		}
	}
}
