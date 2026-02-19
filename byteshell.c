#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <pwd.h>
#include <termios.h>

#define SHELL_MAX_INPUT 1024
#define MAX_ARGS 64
#define MAX_HISTORY 100
#define BYTESHELL_VERSION "1.0"

// Color codes
#define COLOR_RESET "\033[0m"
#define COLOR_USER "\033[1;32m"  // Green
#define COLOR_PATH "\033[1;34m"   // Blue

// Forward declarations - declare all functions first!
void print_prompt(void);
void enable_raw_mode(void);
void restore_terminal(void);
void clear_line(void);
void redraw_line(char *buffer, int pos);
void add_to_history(char *cmd);
char* get_from_history(int offset);
char* read_input_with_history(void);
void sigint_handler(int sig);
void cleanup_history(void);

// Built-in command function declarations
int byteshell_cd(char **args);
int byteshell_exit(char **args);
int byteshell_help(char **args);
int byteshell_clear(char **args);
int byteshell_pwd(char **args);
int byteshell_echo(char **args);
int byteshell_history(char **args);

// Built-in commands structure
typedef struct {
    char *name;
    int (*func)(char **args);
    char *help;
} builtin_t;

// Terminal settings
struct termios orig_termios;

// History storage
char *history[MAX_HISTORY];
int history_count = 0;
int history_position = -1;

// Current input buffer
char current_input[SHELL_MAX_INPUT];
int input_pos = 0;

// Built-in commands table
builtin_t builtins[] = {
    {"cd", byteshell_cd, "Change directory"},
    {"exit", byteshell_exit, "Exit ByteShell"},
    {"quit", byteshell_exit, "Exit ByteShell"},
    {"help", byteshell_help, "Show this help message"},
    {"clear", byteshell_clear, "Clear the screen"},
    {"pwd", byteshell_pwd, "Print working directory"},
    {"echo", byteshell_echo, "Print arguments"},
    {"history", byteshell_history, "Show command history"},
    {NULL, NULL, NULL}
};

// Restore terminal settings
void restore_terminal() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// Enable raw mode
void enable_raw_mode() {
    struct termios raw;
    
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(restore_terminal);
    
    raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Clear current line
void clear_line() {
    printf("\r\033[K");  // Carriage return + clear line
    fflush(stdout);
}

// Print prompt
void print_prompt() {
    char cwd[SHELL_MAX_INPUT];
    char *username;
    
    getcwd(cwd, sizeof(cwd));
    username = getenv("USER");
    if (!username) username = "user";
    
    char *home = getenv("HOME");
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        printf("%s%s%s:%s~%s%s $ ", 
               COLOR_USER, username, COLOR_RESET,
               COLOR_PATH, cwd + strlen(home), COLOR_RESET);
    } else {
        printf("%s%s%s:%s%s%s $ ", 
               COLOR_USER, username, COLOR_RESET,
               COLOR_PATH, cwd, COLOR_RESET);
    }
    fflush(stdout);
}

// Add command to history
void add_to_history(char *cmd) {
    if (strlen(cmd) == 0) return;
    
    // Don't add duplicate of last command
    if (history_count > 0 && strcmp(history[history_count-1], cmd) == 0) {
        return;
    }
    
    if (history_count < MAX_HISTORY) {
        history[history_count] = strdup(cmd);
        history_count++;
    } else {
        // Rotate history
        free(history[0]);
        for (int i = 1; i < MAX_HISTORY; i++) {
            history[i-1] = history[i];
        }
        history[MAX_HISTORY-1] = strdup(cmd);
    }
    history_position = history_count;  // Reset position to end
}

// Get from history (offset: -1 for previous, +1 for next)
char* get_from_history(int offset) {
    int new_pos = history_position + offset;
    
    if (new_pos >= 0 && new_pos < history_count) {
        history_position = new_pos;
        return history[history_position];
    }
    return NULL;
}

// Redraw input line
void redraw_line(char *buffer, int pos) {
    clear_line();
    print_prompt();
    printf("%s", buffer);
    // Note: Full cursor positioning would need more code
    fflush(stdout);
}

// Signal handler
void sigint_handler(int sig) {
    printf("\n");
    print_prompt();
    fflush(stdout);
    input_pos = 0;
    current_input[0] = '\0';
}

// Read input with arrow key support
char* read_input_with_history() {
    static char buffer[SHELL_MAX_INPUT];  // Make static so we can return it
    char c;
    int pos = 0;
    
    buffer[0] = '\0';
    
    while (1) {
        c = getchar();
        
        if (c == '\n') {
            // Enter key
            printf("\n");
            buffer[pos] = '\0';
            strcpy(current_input, buffer);
            input_pos = pos;
            return buffer;
        }
        else if (c == 127 || c == '\b') {
            // Backspace
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
        }
        else if (c == 4) {
            // Ctrl+D
            return NULL;
        }
        else if (c == 3) {
            // Ctrl+C
            printf("\n");
            print_prompt();
            fflush(stdout);
            pos = 0;
            buffer[0] = '\0';
        }
        else if (c == 27) {  // Escape sequence (arrow keys)
            c = getchar();  // '['
            if (c == '[') {
                c = getchar();  // 'A', 'B', 'C', 'D'
                
                if (c == 'A') {  // Up arrow - previous history
                    char *hist_cmd = get_from_history(-1);
                    if (hist_cmd) {
                        strcpy(buffer, hist_cmd);
                        pos = strlen(buffer);
                        redraw_line(buffer, pos);
                    }
                }
                else if (c == 'B') {  // Down arrow - next history
                    char *hist_cmd = get_from_history(1);
                    if (hist_cmd) {
                        strcpy(buffer, hist_cmd);
                        pos = strlen(buffer);
                        redraw_line(buffer, pos);
                    } else {
                        // At the end of history, clear the line
                        buffer[0] = '\0';
                        pos = 0;
                        redraw_line(buffer, pos);
                    }
                }
                // Left/right arrows could be added here
            }
        }
        else if (c >= 32 && c < 127 && pos < SHELL_MAX_INPUT - 1) {
            // Regular characters
            buffer[pos++] = c;
            printf("%c", c);
            fflush(stdout);
        }
    }
}

// Parse command
int parse_command(char *line, char **args) {
    int i = 0;
    char *token = strtok(line, " ");
    
    while (token != NULL && i < MAX_ARGS - 1) {
        args[i] = token;
        i++;
        token = strtok(NULL, " ");
    }
    args[i] = NULL;
    return i;
}

// Check if built-in
int is_builtin(char *cmd) {
    for (int i = 0; builtins[i].name != NULL; i++) {
        if (strcmp(cmd, builtins[i].name) == 0) {
            return 1;
        }
    }
    return 0;
}

// Execute built-in
int exec_builtin(char **args) {
    for (int i = 0; builtins[i].name != NULL; i++) {
        if (strcmp(args[0], builtins[i].name) == 0) {
            return builtins[i].func(args);
        }
    }
    return -1;
}

// Built-in: cd
int byteshell_cd(char **args) {
    if (args[1] == NULL) {
        chdir(getenv("HOME"));
    } else {
        if (chdir(args[1]) != 0) {
            perror("cd");
        }
    }
    return 1;
}

// Built-in: exit
int byteshell_exit(char **args) {
    printf("Goodbye from ByteShell!\n");
    exit(0);
    return 0;
}

// Built-in: help
int byteshell_help(char **args) {
    printf("\nByteShell v%s - Commands:\n", BYTESHELL_VERSION);
    printf("===========================\n");
    for (int i = 0; builtins[i].name != NULL; i++) {
        printf("  %-8s - %s\n", builtins[i].name, builtins[i].help);
    }
    printf("  Ctrl+C: Cancel current line\n");
    printf("  Ctrl+D: Exit ByteShell\n\n");
    return 1;
}

// Built-in: clear
int byteshell_clear(char **args) {
    printf("\033[2J\033[H");
    return 1;
}

// Built-in: pwd
int byteshell_pwd(char **args) {
    char cwd[SHELL_MAX_INPUT];
    getcwd(cwd, sizeof(cwd));
    printf("%s\n", cwd);
    return 1;
}

// Built-in: echo
int byteshell_echo(char **args) {
    for (int i = 1; args[i] != NULL; i++) {
        printf("%s ", args[i]);
    }
    printf("\n");
    return 1;
}

// Built-in: history
int byteshell_history(char **args) {
    printf("\nCommand History:\n");
    printf("================\n");
    for (int i = 0; i < history_count; i++) {
        printf("%4d  %s\n", i + 1, history[i]);
    }
    printf("\n");
    return 1;
}

// Execute external command
int execute_command(char **args) {
    pid_t pid = fork();
    
    if (pid == 0) {
        execvp(args[0], args);
        printf("ByteShell: command not found: %s\n", args[0]);
        exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        return 1;
    } else {
        perror("fork");
        return -1;
    }
}

// Clean up history
void cleanup_history() {
    for (int i = 0; i < history_count; i++) {
        free(history[i]);
    }
}

int main() {
    char *input;
    
    // Set up signal handler
    signal(SIGINT, sigint_handler);
    
    // Enable raw mode
    enable_raw_mode();
    
    // Welcome message
    printf("╔══════════════════════╗\n");
    printf("║ByteShell v%s on Termux ║\n", BYTESHELL_VERSION);
    printf("║                         ║\n");
    printf("║                         ║\n");
    printf("║                         ║\n");
    printf("╚══════════════════════╝\n");
    printf("Type 'help' for commands\n\n");
    
    // Main loop
    while (1) {
        print_prompt();
        
        // Read input with history navigation
        input = read_input_with_history();
        
        if (!input) {  // Ctrl+D
            printf("\n");
            break;
        }
        
        // Skip empty
        if (strlen(input) == 0) {
            continue;
        }
        
        // Add to history
        add_to_history(input);
        
        // Parse and execute
        char *args[MAX_ARGS];
        char *input_copy = strdup(input);  // Make a copy for parsing
        int arg_count = parse_command(input_copy, args);
        
        if (arg_count > 0) {
            if (is_builtin(args[0])) {
                exec_builtin(args);
            } else {
                execute_command(args);
            }
        }
        
        free(input_copy);
    }
    
    // Cleanup
    restore_terminal();
    cleanup_history();
    
    return 0;
}
