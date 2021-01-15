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

char *path = "/";

// Note: This function returns a pointer to a substring of the original string.
// If the given string was allocated dynamically, the caller must not overwrite
// that pointer with the returned value, since the original pointer must be
// deallocated using the same allocator with which it was allocated.  The return
// value must NOT be deallocated using free() etc.
char *trimwhitespace(char *str)
{
    char *end;

    // Trim leading space
    while (isspace((unsigned char)*str))
        str++;

    if (*str == 0) // All spaces?
        return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;

    // Write new null terminator character
    end[1] = '\0';

    return str;
}

#define FALSE 0
#define TRUE 1

#define OFFSET_SIZE 1024
char *exec_to_buffer(char **args, int outputOutput, int outputError)
{
    int pipefd[2];
    pipe(pipefd);
    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork");
        return NULL;
    }

    else if (pid == 0)
    {
        close(pipefd[0]);

        if (outputOutput)
            dup2(pipefd[1], 1);
        if (outputError)
            dup2(pipefd[1], 2);

        close(pipefd[1]);
        execvp(args[0], args);
    }
    else
    {
        wait(NULL);
        char *buffer = malloc(OFFSET_SIZE * sizeof *buffer);

        close(pipefd[1]);
        int offset = 0;
        while (read(pipefd[0], buffer + offset, OFFSET_SIZE) != 0)
        {
            offset += OFFSET_SIZE;
            char *p = realloc(buffer, (offset + OFFSET_SIZE) * sizeof *buffer);
            if (!p)
            {
                perror("Realloc issues");
            }
            else
            {
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
char **split_string(int *argc, char *line, char *delimiter)
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

    token = strtok(line, delimiter);
    while (token != NULL)
    {
        (*argc)++;
        tokens[position] = trimwhitespace(token);
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

        token = strtok(NULL, delimiter);
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
int cd(char **args);

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
    "lrm",
    "cd"};

int (*builtin_func[])(char **) = {
    &lcd,
    &redirect_launch,
    &redirect_launch,
    &redirect_launch,
    &help,
    &sh_exit,
    &sh_exit,
    &lmkdir,
    &lrm,
    &cd};

int num_builtins()
{
    return sizeof(builtin_str) / sizeof(char *);
}

/*
  Builtin function implementations.
*/
int cd(char **args)
{
    char *cd_path = path;
    if (args[1] == NULL)
    {
        fprintf(stderr, "dbsh: expected path\n");
        return 1;
    }

    char *ls_args[] = {"./dbxcli", "ls", NULL};
    char *buffer = exec_to_buffer(ls_args, 1, 1);
    int num_files;

    char **files = split_string(&num_files, buffer, "/");

    int exists = 0;
    for (int i = 0; i < num_files; i++)
    {
        if (strcmp(files[i], args[1]) == 0)
        {
            exists = 1;
        }
    }

    if (!exists)
    {
        fprintf(stderr, "dbsh: path doesn't exist\n");
    }
    else
    {
        char *p = realloc(cd_path, strlen(args[1]) * sizeof *cd_path);
        if (!p)
        {
            perror("Realloc issues");
        }
        else
        {
            cd_path = p;
        }
        strcat(cd_path, args[1]);
        printf("%s%s", cd_path, args[1]);
    }

    return 1;
}

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
        args = split_string(&argc, line, TOK_DELIM);
        status = execute(argc, args);
        free(line);
        free(args);
    } while (status);
}

void login()
{
    int argc;
    int status;
    char *buf;
    char *args[] = {"./dbxcli", "account", NULL};
    do
    {
        buf = exec_to_buffer(args, FALSE, TRUE);
        printf("%s", buf);
    } while (strlen(buf));
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