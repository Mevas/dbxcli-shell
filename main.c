#include <stdlib.h>
#include <sys/wait.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <regex.h>
#include <fcntl.h>
#include <errno.h>

#define FALSE 0
#define TRUE 1

#define OFFSET_SIZE 1024
char* exec_to_buffer(char **args, int outputOutput, int outputError) {
    int pipefd[2];
    pipe(pipefd);
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return NULL;
    }

    else if (pid == 0) {
        close(pipefd[0]);

        if(outputOutput) dup2(pipefd[1], 1);
        if(outputError) dup2(pipefd[1], 2);

        close(pipefd[1]);
        execvp(args[0], args);
    }
    else {
        wait(NULL);
        char *buffer = malloc(OFFSET_SIZE * sizeof *buffer);

        close(pipefd[1]);
        int offset = 0;
        while (read(pipefd[0], buffer + offset, OFFSET_SIZE) != 0)
        {
            offset += OFFSET_SIZE;
            char *p = realloc(buffer, (offset + OFFSET_SIZE) * sizeof * buffer);
            if (!p) {
                perror("Realloc issues");
            } else {
                buffer = p;
            }
        }

        close(pipefd[0]);
        return buffer;
    }
}

// Reading

char *read_line(void)
{
    char *line = NULL;
    size_t bufsize = 0; // have getline allocate a buffer for us

    if (getline(&line, &bufsize, stdin) == -1)
    {
        if (feof(stdin))
        {
            exit(EXIT_SUCCESS); // We recieved an EOF
        }
        else
        {
            perror("readline");
            exit(EXIT_FAILURE);
        }
    }

    return line;
}

#define TOK_BUFSIZE 64
#define TOK_DELIM " \t\r\n\a"
char **split_line(int *argc, char *line)
{
    (*argc) = 0;
    int bufsize = TOK_BUFSIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char *));
    char *token;

    if (!tokens)
    {
        fprintf(stderr, "dbsh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, TOK_DELIM);
    while (token != NULL)
    {
        (*argc)++;
        tokens[position] = token;
        position++;

        if (position >= bufsize)
        {
            bufsize += TOK_BUFSIZE;
            tokens = realloc(tokens, bufsize * sizeof(char *));
            if (!tokens)
            {
                fprintf(stderr, "dbsh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

// Process launcher

int launch(char **args)
{
    pid_t pid, wpid;
    int status;

    pid = fork();
    if (pid == 0)
    {
        // Child process
        if (execvp(args[0], args) == -1)
        {
            perror("dbsh");
        }
        exit(EXIT_SUCCESS);
    }
    else if (pid < 0)
    {
        // Error forking
        perror("dbsh");
    }
    else
    {
        // Parent process
        do
        {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}

int db_launch(int argc, char **args)
{
    for (int i = 0; i < argc; i++)
    {
        args[i + 1] = args[i];
    }
    args[0] = "./dbxcli";

    launch(args);
}

// Built-ins

// Remove first char from function name(Usage: lls - launch(ls))
int redirect_launch(char **args)
{
    args[0]++;
    int retVal = launch(args);
    args[0]--;
    return retVal;
}

/*
  Function Declarations for builtin shell commands:
 */
int lcd(char **args);
int help(char **args);
int sh_exit(char **args);
int lmkdir(char **args);
int lls(char **args);
int lrm(char **args);

/*
  List of builtin commands, followed by their corresponding functions.
 */
char *builtin_str[] = {
    "lcd",
    "lls",
    "lmv",
    "lpwd",
    "help",
    "exit",
    "quit",
    "lmkdir",
    "lrm"};

int (*builtin_func[])(char **) = {
    &lcd,
    &redirect_launch,
    &redirect_launch,
    &redirect_launch,
    &help,
    &sh_exit,
    &sh_exit,
    &lmkdir,
    &lrm};

int num_builtins()
{
    return sizeof(builtin_str) / sizeof(char *);
}

/*
  Builtin function implementations.
*/
int lcd(char **args)
{
    if (args[1] == NULL)
    {
        fprintf(stderr, "dbsh: expected argument to \"lcd\"\n");
    }
    else
    {
        if (chdir(args[1]) != 0)
        {
            perror("dbsh");
        }
    }
    return 1;
}

int lmkdir(char **args)
{
    if (args[1] == NULL)
    {
        fprintf(stderr, "dbsh: expected directory name to be passed to \"lmkdir\"\n");
    }
    else
    {
        if (mkdir(args[1], S_IRWXU) != 0)
        {
            perror("dbsh");
        }
    }
    return 1;
}

int lrm(char **args)
{
    if (args[1] == NULL)
    {
        fprintf(stderr, "dbsh: expected directory or file name to be passed to \"lrm\"\n");
    }
    else
    {
        if (remove(args[1]) != 0)
        {
            perror("dbsh");
        }
    }
    return 1;
}

int help(char **args)
{
    int i;
    printf("Wrapper shell for dbxcli\n");
    printf("The following are built in:\n");

    for (i = 0; i < num_builtins(); i++)
    {
        printf("  %s\n", builtin_str[i]);
    }
    return 1;
}

int sh_exit(char **args)
{
    return 0;
}

// Shell executor

int execute(int argc, char **args)
{
    int i;

    if (args[0] == NULL)
    {
        // An empty command was entered.
        return 1;
    }

    for (i = 0; i < num_builtins(); i++)
    {
        if (strcmp(args[0], builtin_str[i]) == 0)
        {
            return (*builtin_func[i])(args);
        }
    }

    db_launch(argc, args);
    printf("dbsh: unknown command %s, run \"help\" for usage\n", args[0]);
    return -1;
}

// Main shell loop

void loop(void)
{
    char *line;
    char **args;
    int status;

    do
    {
        int argc = 0;
        printf("> ");
        line = read_line();
        args = split_line(&argc, line);
        status = execute(argc, args);
        free(line);
        free(args);
    } while (status);
}

void login() {
    int argc;
    int status;
    char *buf;
    char *args[] = {"./dbxcli", "account", NULL};
    do{       
        buf = exec_to_buffer(args, FALSE, TRUE);
        printf("%s", buf);
    }while(strlen(buf));
}

int main(int argc, char **argv)
{
    // Load config files, if any.
    login();
    // Run command loop.
    loop();

    // Perform any shutdown/cleanup.

    return EXIT_SUCCESS;
}