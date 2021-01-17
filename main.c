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

const char *DB_COMMAND = "./dbxcli";

//TODO: either we start from empty string and when cd we append first '/'
//OR we make sure not to print last char of path and always end it with '/'
//NOW it always end with '/' and we print the whole path
//CHANGED: this has to be dinamically allocated with malloc else we can't realloc
char *path = NULL;

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

int startsWith(const char *pre, const char *str)
{
    size_t lenpre = strlen(pre),
           lenstr = strlen(str);
    return lenstr < lenpre ? FALSE : memcmp(pre, str, lenpre) == 0;
}

//Note: pass target as refference
//TODO: prob better/safer to return new address, no more need to pass by ref
void str_resize_cat(char **target, char *addition)
{
    if (*target == NULL)
    {
        *target = malloc(strlen(addition));
        strcpy(*target, addition);

        return;
    }

    char *p = realloc(*target, strlen(*target) + strlen(addition));
    if (!p)
    {
        //TODO: this is bad :(
    }
    *target = p;
    strcat(*target, addition);
    return 0;
}

#define OFFSET_SIZE 1024

char *exec_to_buffer(char **args, int outputOutput, int outputError)
{
    int pipefd[2];
    pipe(pipefd); //pipefd[0] for reading, pipefd[1] for writing
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
            dup2(pipefd[1], STDOUT_FILENO);
        if (outputError)
            dup2(pipefd[1], STDERR_FILENO);

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
    char *dbxargs[argc + 2];
    dbxargs[0] = DB_COMMAND;
    for (int i = 0; i < argc; i++)
    {
        dbxargs[i + 1] = args[i];
    }

    dbxargs[argc + 1] = NULL;

    launch(dbxargs);
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
//TODO: integrate path with the rest of the dbxcli commands
int cd(char **args)
{
    if (args[1] == NULL)
    {
        fprintf(stderr, "dbsh: expected path\n");
        return -1;
    }

    char *cd_path = NULL;
    str_resize_cat(&cd_path, path);
    //TODO: check for ../
    str_resize_cat(&cd_path, args[1]);

    char *ls_args[] = {"./dbxcli", "ls", cd_path, NULL};
    char *buffer = exec_to_buffer(ls_args, TRUE, TRUE);
    //TODO : see for error or smth
    if (startsWith("Error", buffer))
    {
        fprintf(stderr, "dbsh: path doesn't exist\n");
        return -1;
    }

    str_resize_cat(&cd_path, "/");
    path = realloc(path, strlen(cd_path));
    strcpy(path, cd_path);
    //printf("%s%s", cd_path, args[1]);

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
    //printf("dbsh: unknown command %s, run \"help\" for usage\n", args[0]);
    return -1;
}

void print_path()
{
    char *formatted_path = malloc(strlen(path));
    strcpy(formatted_path, path);
    if (strlen(formatted_path) > 1)
    {
        formatted_path[strlen(formatted_path) - 1] = 0;
    }
    printf("%s> ", formatted_path);
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
        print_path();
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
    str_resize_cat(&path, "/");
    // Load config files, if any.
    login();
    // Run command loop.
    loop();

    // Perform any shutdown/cleanup.

    return EXIT_SUCCESS;
}