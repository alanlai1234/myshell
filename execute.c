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

int execute(command *start, int len){
    char *input[] = {"/bin/echo", "hello", NULL};

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
