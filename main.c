/* 
*   Smallshell
*   Yannis Giamakidis
*/

#include <unistd.h> // Library used for getpid, getppid, fork, exec family functions, wait
#include <stdio.h>  // Library used for printf, perror
#include <sys/types.h>  // Library used for pid_t
#include <stdlib.h> // Library used for exit
#include <sys/wait.h> // Library used for wait, waitpid
#include <string.h> // Library used for strtok
#include <stdbool.h> // Library used for bool
#include <fcntl.h> 
#include <signal.h> // Library used for signal handling
#include <sys/stat.h>

/*
 * Global variable to keep track of foreground mode status.
 * It is recommended to use volatile sig_atomic_t for variables accessed in signal handlers.
 */
volatile sig_atomic_t foreground_mode = 0;

void child_signal_handler(int signalNumber) {

    if (signalNumber == SIGINT) {

        write(STDOUT_FILENO, "Child process received SIGINT\n", 30);
        exit(0);    // Exit the child process
    }
}

void parent_signal_handler(int signo) {
    const char enterMsg[] = "\nEntering foreground-only mode (& is ignored)\n";
    const char exitMsg[] = "\nExiting foreground-only mode\n";

    if (foreground_mode == 0) {
        foreground_mode = 1;
        write(STDOUT_FILENO, enterMsg, sizeof(enterMsg) - 1);
    } else {
        foreground_mode = 0;
        write(STDOUT_FILENO, exitMsg, sizeof(exitMsg) - 1);
    }
}

// Start of replace string functions
int is_substring_match(const char *sourceString, const char *targetString, int startIndex) {

    int sourceIndex = 0;
    int targetIndex = 0;

    for (sourceIndex = startIndex, targetIndex = 0;
        targetString[targetIndex] != '\0';
        ++sourceIndex, ++targetIndex) {

            if (sourceString[sourceIndex] != targetString[targetIndex]) {

                return 0;   // Not a match
            }
        }
    
    return 1;   // Match found 
}

void copy_replacement(char *outputString, const char *replacementString, int *writeIndex) {

    for (int replaceIndex = 0; replacementString[replaceIndex] != '\0'; ++replaceIndex) {

        outputString[*writeIndex] = replacementString[replaceIndex];
        ++(*writeIndex);
    }
}

char* replace_string(const char* sourceStr, const char* targetStr, const char* replacementString) {

    char *output = malloc(2048);

    if (!output) {
        return NULL;    // This means the memory allocation failed
    }

    int readIndex;
    int writeIndex;
    for (readIndex = 0, writeIndex = 0; sourceStr[readIndex] != '\0';) {

        if (sourceStr[readIndex] != targetStr[0]) {

            output[writeIndex++] = sourceStr[readIndex++];    
        }
        else {

            if (is_substring_match(sourceStr, targetStr, readIndex)) {

                copy_replacement(output, replacementString, &writeIndex);
                readIndex += strlen(targetStr);
            }
            else {

                output[writeIndex++] = sourceStr[readIndex++];
            }
        }
    }
    output[writeIndex] = '\0';
    return output;
}

// Start of execCommands functions
void configureIOredirection(const char* inFile, const char* outFile) {
    int inFileDescriptor, outFileDescriptor, duplicateFileDescriptor;
    
    if (inFile != NULL) {
        inFileDescriptor = open(inFile, O_RDONLY);
        if (inFileDescriptor == -1) {
            perror("Error opening input file");
            exit(1);
        }
        duplicateFileDescriptor = dup2(inFileDescriptor, STDIN_FILENO);
        if (duplicateFileDescriptor == -1) {
            perror("Error duplicating input file descriptor");
            exit(2);
        }
        fcntl(inFileDescriptor, F_SETFD, FD_CLOEXEC);
    }

    if (outFile != NULL) {
        outFileDescriptor = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if (outFileDescriptor == -1) {
            perror("Error opening output file");
            exit(1);
        }
        duplicateFileDescriptor = dup2(outFileDescriptor, STDOUT_FILENO);
        if (duplicateFileDescriptor == -1) {
            perror("Error duplicating output file descriptor");
            exit(2);
        }
        fcntl(outFileDescriptor, F_SETFD, FD_CLOEXEC);
    }
}

void run_child_process(char* commandArgs[], const char* stdinRedirect, const char* stdoutRedirect, int isBackgroundProcess) {
    signal(SIGTSTP, SIG_IGN);  // Ignore SIGTSTP in child process
    
    // Configure background process behavior
    if (isBackgroundProcess == 1) {
        if (stdinRedirect == NULL) {
            stdinRedirect = "/dev/null";  // Redirect stdin to /dev/null if not specified
        }
        if (stdoutRedirect == NULL) {
            stdoutRedirect = "/dev/null"; // Redirect stdout to /dev/null if not specified
        }
        signal(SIGINT, SIG_IGN); // Ignore SIGINT in background process
    } else {
        signal(SIGINT, child_signal_handler);  // Custom SIGINT handler for foreground process
    }

    // Set up redirection for stdin and stdout
    configureIOredirection(stdinRedirect, stdoutRedirect);

    // Execute the command
    execvp(commandArgs[0], commandArgs);
    perror("Error executing command");
    exit(EXIT_FAILURE);
}

void run_parent_process(int spawnedChildPid, int *childProcStatus, int isBackgroundProcess) {

    if (isBackgroundProcess == 1) {
        printf("Background process PID is: %d\n", spawnedChildPid);
    } else {
        // Wait for the child process to complete
        waitpid(spawnedChildPid, childProcStatus, 0);

        // Check if the child process exited normally
        if (WIFEXITED(*childProcStatus)) {
            int statusCode = WEXITSTATUS(*childProcStatus);
            if (statusCode != 0) {
                printf("Command not found or exited with error code\n");
            }
        } 
        // Check if the child process was terminated by a signal
        else if (WIFSIGNALED(*childProcStatus)) {
            printf("Terminated by signal %d\n", WTERMSIG(*childProcStatus));
        }
    }
}

void exec_commands(char* inputs[], int *background, char *inputName, char *outputName, int* childStatus) {

    pid_t pid_of_child = fork();

    switch(pid_of_child) {
        case -1:
            perror("Fork failed");
            break;
        case 0:
            run_child_process(inputs, inputName, outputName, *background);
            break;
        default:
            run_parent_process(pid_of_child, childStatus, *background);
            break;
    }
}

/*
 * void monitorBackgroundProcesses()
 * Monitors background processes, printing the PID and exit status or termination signal
 * when a background process finishes.
 */
void monitor_background_processes() {

    int status;
    pid_t completedPid;

    while (1) {

        completedPid = waitpid(-1, &status, WNOHANG); // Wait for any child process

        if (completedPid == 0) {

            // No child process has exited
            return;
        } else if (completedPid == -1) {

            // An error occurred in waitpid
            return;
        } else {

            if (WIFEXITED(status)) {

                // Child process exited normally
                printf("Background PID %d is done: exit value %d\n", completedPid, WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {

                // Child process terminated by a signal
                printf("Background PID %d is done: terminated by signal %d\n", completedPid, WTERMSIG(status));
            }
            fflush(stdout);
        }
    }
}

/*
 * void reportChildExitStatus(int childExitStatus)
 * Reports how the child process exited, whether normally or due to a signal.
 */
void report_child_exit_status(int childExitStatus) {

    if (WIFEXITED(childExitStatus)) {
        // Child process exited normally
        printf("Exit value %d\n", WEXITSTATUS(childExitStatus));
    } else if (WIFSIGNALED(childExitStatus)) {
        // Child process was terminated by a signal
        printf("Terminated by signal %d\n", WTERMSIG(childExitStatus));
    }
}

// Reading and validing the user input
void read_user_input(char userInput[], int maxInputSize) {
    
    bool isInputEmpty = false;
    bool isInputComment = false;

    while (true) {

        printf(": ");
        fflush(stdout);

        if (fgets(userInput, maxInputSize, stdin) == NULL) {

            break;
        }

        isInputEmpty = strncmp(userInput, "\n", 1) == 0;
        isInputComment = strncmp(userInput, "#", 1) == 0;

        if (!isInputEmpty && !isInputComment) { // If the input is not empty and not a comment (valid input), break the loop

            break;
        }
    }
}

void parse_special_symbols(char **currentToken, int *isBackgroundProcess, char **redirectInputName, char **redirectOutputName, char *pid_string, char seperators[]) {

    char *nextToken;

    if (strncmp(*currentToken, "<", 2048) == 0) {

        nextToken = strtok(NULL, seperators);
        *redirectInputName = replace_string(nextToken, "$$", pid_string);
    }
    else if (strncmp(*currentToken, ">", 2048) == 0) {

        nextToken = strtok(NULL, seperators);
        *redirectOutputName = replace_string(nextToken, "$$", pid_string);
    }
    else if (strncmp(*currentToken, "&", 2048) == 0) {

        *isBackgroundProcess = (!foreground_mode) ? 1 : 0;
    }
}

void tokenize_and_store(char userInput[], char *parsedArgs[], int *isBackgroundProcess, char **redirectInputName, char **redirectOutputName, char *pid_string) {

    char delimiters[] = " \t\n";
    char *currentToken = strtok(userInput, delimiters);
    parsedArgs[0] = strdup(currentToken);
    int argIndex = 1;

    while ((currentToken = strtok(NULL, delimiters)) != NULL) {

        parse_special_symbols(&currentToken, isBackgroundProcess, redirectInputName, redirectOutputName, pid_string, delimiters);
        
        if (currentToken != NULL) {

            parsedArgs[argIndex++] = replace_string(currentToken, "$$", pid_string);
        }
    }
}

void get_the_input(char *parsedArgs[], int *isBackgroundProcess, char **redirectInputName, char **redirectOutputName, int pid) {

    char userInput[2048];
    char pid_string[128];
    sprintf(pid_string, "%d", pid);

    read_user_input(userInput, 2048);

    *isBackgroundProcess = 0;
    *redirectInputName = NULL;
    *redirectOutputName = NULL;

    tokenize_and_store(userInput, parsedArgs, isBackgroundProcess, redirectInputName, redirectOutputName, pid_string);

}

int main() {

    int running = 1;
    int pid = getpid();
    char* inputFile;
    char* outputFile;
    char* input[512];
    int background = 0;
    int i;
    int exitStatus = 0;

        // Installing signal handlers
    signal(SIGCHLD, monitor_background_processes); // Handle background process completion
    signal(SIGINT, SIG_IGN); // Ignore SIGINT (Ctrl+C)
    signal(SIGTSTP, parent_signal_handler); // Custom handler for SIGTSTP (Ctrl+Z)

    while (running) {
        for (i = 0; i < 512; ++i)
            input[i] = NULL;

        get_the_input(input, &background, &inputFile, &outputFile, pid);

        if (strncmp(input[0], "exit", 2048) == 0) {
            running = 0;
        }
        else if (strncmp(input[0], "cd", 2048) == 0) {
            if(input[1] != NULL) {
                char buff[2048];
                getcwd(buff, 2048);
                strcat(buff, "/");
                strcat(buff, input[1]);
                chdir(buff);
            }
            else {
                chdir(getenv("HOME"));
            }
        }
        else if(strncmp(input[0], "status", 2048) == 0) {
            report_child_exit_status(exitStatus);
        }
        else {
            exec_commands(input, &background, inputFile, outputFile, &exitStatus);
        }
    }
    
    return 0;
}