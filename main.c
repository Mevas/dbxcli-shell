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

char *path = NULL;
char **path_array;
int path_length = 0;

char *trimwhitespace(char *str)
{
    char *end;

    // Trim leading space
    while (isspace((unsigned char)*str))
        str++;

    if (*str == 0)
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
        perror("Something went wrong");
        return;
    }
    *target = p;
    strcat(*target, addition);
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
    size_t bufsize = 0;

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

char *get_path_string(char **_path_array, int _path_length)
{
    char *path_string = NULL;

    if (_path_length == 0)
    {
        str_resize_cat(&path_string, "/");
    }

    for (int i = 0; i < _path_length; i++)
    {
        str_resize_cat(&path_string, "/");
        str_resize_cat(&path_string, _path_array[i]);
    }
    return path_string;
}

char **move_into_folder(char **cd_path_array, int *cd_path_length, char *name)
{
    // Move back
    if (strcmp(name, "..") == 0)
    {
        if (*cd_path_length > 0)
        {
            free(cd_path_array[--(*cd_path_length)]);
        }
        return cd_path_array;
    }

    // Append the new folder name to the path
    char **new_path_array;
    new_path_array = realloc(cd_path_array, (++(*cd_path_length)) * sizeof(char *));
    new_path_array[(*cd_path_length) - 1] = malloc(strlen(name));
    strcpy(new_path_array[(*cd_path_length) - 1], name);

    return new_path_array;
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

    char **cd_path_array = malloc(path_length * sizeof(char *));
    int cd_path_length = path_length;

    // Copy the global path into a local variable
    for (int i = 0; i < path_length; i++)
    {
        cd_path_array[i] = malloc(strlen(path_array[i]));
        strcpy(cd_path_array[i], path_array[i]);
    }

    int names_length = 0;
    char **names = split_string(&names_length, args[1], "/");

    // For each folder name, compute the next path
    for (int i = 0; i < names_length; i++)
    {
        char **p = move_into_folder(cd_path_array, &cd_path_length, names[i]);
        if (!p)
        {
            perror("Something went wrong");
            return -1;
        }
        cd_path_array = p;
    }

    // Check if path exists
    char *ls_args[] = {"./dbxcli", "ls", get_path_string(cd_path_array, cd_path_length), NULL};
    char *buffer = exec_to_buffer(ls_args, TRUE, TRUE);

    if (startsWith("Error", buffer))
    {
        fprintf(stderr, "dbsh: path doesn't exist\n");
        return -1;
    }

    // Free the global path
    for (int i = 0; i < path_length; i++)
    {
        free(path_array[i]);
    }
    free(path_array);

    // Put local computated path into the global path
    path_array = malloc(cd_path_length * sizeof(char *));
    for (int i = 0; i < cd_path_length; i++)
    {
        path_array[i] = malloc(strlen(cd_path_array[i]));
        strcpy(path_array[i], cd_path_array[i]);
    }
    path_length = cd_path_length;

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
    printf("%s> ", get_path_string(path_array, path_length));
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