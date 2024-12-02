#include <stdio.h>  //standard I/O 
#include <stdlib.h> //memory managment
#include <string.h> //strtok, strchr, strcmp
#include <unistd.h> //fork,chdir,execv,dup2, getcwd
#include <fcntl.h> // open
#include <sys/wait.h> //waitpid
#include <sys/types.h> //the data types in system calls 
#include <sys/stat.h> //definitions in regards to file status
#include <dirent.h> //opendir, readdir
#include <errno.h> //perror
#include <signal.h> //the signal handling
#include <fnmatch.h> //fnmatch

#define PROMPT "mysh> " 
#define BUFSIZE 1024 
#define MAX_ARGS 100
#define PATHS "/usr/local/bin:/usr/bin:/bin"

void change_directory(const char *path); //declaration


void print_error(const char *message){ //prints error message as agrument 
    perror(message);
}

void welcome_message(){ //welcome message 
    printf("Welcome to my shell!\n");
}
void goodbye_message(){ //goodbye message 
    printf("Exiting my shell.\n");
}
void print_working_directory(){ // prints working directory using getcwd
    char cwd[BUFSIZE];
    if (getcwd(cwd, sizeof(cwd))){
        printf("%s\n", cwd);
    } else {
        perror("pwd");
    }
}


char *search_executable(const char *command) { // looks for an executable file in directories 
    static char full_path[BUFSIZE];
    char *paths = strdup(PATHS);
    char *dir = strtok(paths, ":");

    while (dir) {
        snprintf(full_path, BUFSIZE, "%s/%s", dir, command);
        if (access(full_path, X_OK) == 0) {
            free(paths);
            return full_path;
        }
        dir = strtok(NULL, ":");
    }
    free(paths);
    return NULL; // Returns Null if not found 
}
void which_command(const char *command) { // which command for locate full path of executable 
    if (!command) {                       // handles cases like pathname/ missing agruments 
        fprintf(stderr, "which: missing agrument\n");
        return;
    }

    if (strchr(command, '/')) {
        fprintf(stderr, "which: %s is a pathname\n", command);
        return;
    }

    char *path = search_executable(command);
    if (path) {
        printf("%s\n", path);
    } else {
        fprintf(stderr, "which: command not found: %s\n", command);
    }
}

void change_directory(const char *path) { //implements cd command // no path provided error will occur 
    if(!path) {
        fprintf(stderr, "cd: missing agrument \n");
        return;
    }

    if (chdir(path) != 0) {
        perror("cd"); // will handles path change fails 
    }
}

void handle_redirection(char **args, int *in_fd, int *out_fd){ //handles input < and output > redirection 
    for(int i =0; args[i]; i++) {
        if (strcmp(args[i], "<") == 0) {
            *in_fd = open(args[i + 1], O_RDONLY);
            if (*in_fd < 0 ) {
                print_error("Input redirection failed");
                exit(EXIT_FAILURE);
            }
            args[i] = NULL; 
        } else if (strcmp(args[i], ">") == 0) {
            *out_fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0640);
            if (*out_fd < 0) {
                print_error("Output redirection failed");
                exit(EXIT_FAILURE);
            }
            args[i] = NULL;
        }
    }
}

void expand_wildcards(char *token, char **args, int *arg_index) { //expands the wildcards (*) in a command agrument
    DIR *dir = opendir(".");
    if (!dir) {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' && token[0] != '.') continue;

        if (fnmatch(token, entry->d_name, 0) == 0) { //matches filenames and reads current directory using //fnmatch
            args[(*arg_index)++] = strdup(entry->d_name);
        }
    }
    closedir(dir);
}

void execute_command(char **args, int in_fd, int out_fd) { //executes command calling execv //handles redirection of I/O
    if (in_fd != STDIN_FILENO) {
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    }
    if (out_fd != STDIN_FILENO) {
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }

    if(execv(args[0], args) < 0) {
        print_error("Execution failed");
        exit(EXIT_FAILURE);
    }
}

void handle_pipeline(char *line) { // Pipeline commands  //splitting command line two parts 
    char *commands[2];
    char *args1[MAX_ARGS], *args2[MAX_ARGS];
    int pipefd[2];

    //Splits the command into two parts at the '|'

    commands[0] = strtok(line, "|");
    commands[1] = strtok(NULL, "|");

    if (!commands[1]) {
        fprintf(stderr, "Invalid pipeline command\n");
        return;
    }

    //Parse agruments for both commands 

    char *token = strtok(commands[0], " ");
    int arg_index = 0;
    while (token && arg_index < MAX_ARGS -1) {
        args1[arg_index++] = token;
        token = strtok(NULL, " ");
    }
    args1[arg_index] = NULL;

    token = strtok(commands[1], " ");
    arg_index = 0;
    while (token && arg_index < MAX_ARGS -1 ) {
        args2[arg_index++] = token;
        token = strtok(NULL, " ");
    }
    args2[arg_index] = NULL;

    if(pipe(pipefd) < 0) {
        perror("pipe");
        return;
    }

    pid_t pid1 = fork();
    if (pid1 == 0) {
        //1 child process
        close(pipefd[0]); //closed the unused read end 
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        char *executable1 = search_executable(args1[0]);
        if (! executable1) {
            fprintf(stderr, "Command not found: %s\n", args1[0]);
            exit(EXIT_FAILURE);
        }
        args1[0] = executable1;
        execv(args1[0], args1);
        perror("execv");
        exit(EXIT_FAILURE);
    }

    pid_t pid2 = fork();
    if (pid2 == 0) {
        //2 child process
        close(pipefd[1]); //close unuse write endddd
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);

        char *executable2 = search_executable(args2[0]);
        if (!executable2) {
            fprintf(stderr, "Command not found: %s\n", args2[0]);
            exit(EXIT_FAILURE);
        }
        args2[0] = executable2;
        execv(args2[0], args2);
        perror("execv");
        exit(EXIT_FAILURE);
    }

    //Parent process 
    close(pipefd[0]);
    close(pipefd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}

void process_command(char *line) { //proccesses commands (cd,pwd,which,exit)//handles redirection,wildcards, pipeline
    char *args[MAX_ARGS];
    int in_fd = STDIN_FILENO, out_fd = STDOUT_FILENO;

    char *token = strtok(line, " ");
    int arg_index = 0;
    while (token && arg_index < MAX_ARGS -1) {
        if (strchr(token, '*')) {
            expand_wildcards(token, args, &arg_index);
        } else {
            args[arg_index++] = token;
        }
        token = strtok(NULL, " ");
    }
    args[arg_index] = NULL;

    if (!args[0]) return; // Empty command 

    if (strcmp(args[0], "cd") == 0) {
        change_directory(args[1]);
    } else if (strcmp(args[0], "pwd") == 0) {
        print_working_directory();
    } else if (strcmp(args[0], "which") == 0){
        which_command(args[1]);
    } else if (strcmp(args[0], "exit") == 0) {
        goodbye_message();
        exit(EXIT_SUCCESS);
    } else {
        char *executable = search_executable(args[0]);
        if (!executable) {
            fprintf(stderr, "Command not found: %s\n", args[0]);
            return;
        }
        args[0] = executable;

        pid_t pid = fork(); // forks child process to execute command // needs child process to finish
        if(pid == 0) { //child process
            handle_redirection(args, &in_fd, &out_fd);
            execute_command(args, in_fd, out_fd);
        } else if (pid > 0) { //parent process
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                printf("Command failed with code %d\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("Terminated by signal: %d\n", WTERMSIG(status));
            }
        } else {
            perror("fork");
        }
    }
}

void run_shell(FILE *input, int interactive) { // main loop for the shell //interprets commands from a batch fle or stdin // processes executes 
    if (interactive) welcome_message(); //during interactive mode shows prompt n handles the users input

    char buffer[BUFSIZE];
    while(1) {
        if (interactive) printf(PROMPT);
        if (!fgets(buffer, BUFSIZE, input)) break;

        buffer[strcspn(buffer, "\n")] = '\0';
        process_command(buffer);
    }

    if (interactive) goodbye_message();
}

int main(int argc, char *argv[]) { //handles command line agruments for either interactive mode / batch mode 
    if (argc > 2) { //will call run_shell to begin the shell
        fprintf(stderr, "Usage: %s [batch_file]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    FILE *input = stdin;
    int interactive = isatty(STDIN_FILENO);

    if (argc == 2) {
        input = fopen(argv[1], "r");
        if (!input) {
            perror("Failed to open batch file");
            exit(EXIT_FAILURE);
        }
        interactive = 0;
    }

    run_shell(input, interactive);

    if (input != stdin) fclose(input);
    return 0;
}


