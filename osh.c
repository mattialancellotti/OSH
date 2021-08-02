#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define PROMPT "osh>"
#define MAX_LINE 80
#define MAX_CHRONO 128
#define MAX_CMD 15

/* Errors */
#define INPUT_ERROR      (-1)
#define MEM_ERROR        (-2)
#define FORK_ERROR       (-3)
#define GENERAL_ERROR    (-4)
#define CMD_NOT_FOUND    (-5)
#define INVALID_ARGUMENT (-6)
#define EMPTY_HISTORY    (-7)

#define EXIT             (-9)
#define NO_ERRORS        (-10)

#define EXIT_IF(x, y, z)    if (x == y) z;
#define FREE_IF(x)          if (x) free(x);

struct command {
  const char *command;
  const int needs_args;
};

struct error {
  const char *error_msg;
  const int error_code, should_exit;
};

typedef struct command command_t;
typedef struct error error_t;

char *get_input(void);
char *trim_str(const char * /*cmd*/, size_t /*len*/);
char *del_double(const char * /*cmd*/, size_t /*len*/);
static char *copy(const char * /*str*/, size_t /*len*/);
int split_tokens(const char * /*cmd*/, char * /*args*/[]);
static int check_background(char * /*args*/[], size_t /*len*/);
void history(char * /*chrono*/[], size_t /*chrono_len*/, size_t /*lim*/);
static int requires_args(const command_t * /*cmds*/, size_t /*len*/, size_t /*index*/);
static int search_cmd(const command_t * /*cmds*/, size_t /*len*/, const char * /*cmd*/);
static int error_handler(const error_t * /*errs*/, size_t /*len*/, int /*err*/, FILE * /*end_file*/);
int check_code(char * /*args*/[], const command_t * /*cmds*/, size_t /*len_args*/, size_t /*len_cmds*/);
static int extern_code(char * /*args*/[], size_t /*args_len*/, 
    const error_t * /*errs*/, size_t /*errs_len*/);
static int intern_code(const error_t * /*errs*/, size_t /*errs_len*/, 
    char * /*args*/[], size_t /*args_len*/, int /*code_flag*/, 
    char * /*chrono*/[], size_t /*chrono_pos*/);

static void print(char * /*args*/[], size_t /*len*/);
static int pop(char * /*args*/[], size_t /*pos*/, size_t /*len*/);
static int push(char * /*args*/[], const char * /*token*/, size_t /*pos*/);
static int check_content(const char * /*args*/[], size_t /*len*/);
static void init(char * /*args*/[], size_t /*len*/);
static const char **new_arr(char * /*args*/[], size_t /*len*/, size_t /*index*/);
static const char *get(char * /*args*/[], size_t /*len*/, size_t /*el*/);

void free_arr_stack_p(void ** /*to_free*/, size_t /*size*/);

int main(void) {
  /* Commands known by the OSH */
  const command_t cmds[] = {
    { "exit",    .needs_args = 0 },
    { "history", .needs_args = 0 },
    { "!!",      .needs_args = 0 },
    { "!",       .needs_args = 1 }
  };
  size_t cmds_len = sizeof(cmds)/sizeof(cmds[0]);

  /* Error messages known by the OSH */
  const error_t errs[] = { 
    { "I/O Error: Something happened while getting the input\n",      INPUT_ERROR,      .should_exit = 0 },
    { "Memory Error: Something happened while working with memory\n", MEM_ERROR,        .should_exit = 1 },
    { "Internal Error: A sys call failed\n",                          FORK_ERROR,       .should_exit = 1 },
    { "Command not found\n",                                          CMD_NOT_FOUND,    .should_exit = 0 },
    { "Invalid Argument: An argument is invalid\n",                   INVALID_ARGUMENT, .should_exit = 0 },
    { "La history e' vuota\n",                                        EMPTY_HISTORY,    .should_exit = 0 },
    { "",                                                             NO_ERRORS,        .should_exit = 0 },
    { "Something unexpected happened\n",                              GENERAL_ERROR,    .should_exit = 1 }
  };
  size_t errs_len = sizeof(errs)/sizeof(errs[0]);

#define FLAGS (5)

#define ARGS_FLAGS   (0)
#define CODE_FLAGS   (1)
#define PUSH_FLAGS   (2)
#define EXTERN_FLAGS (3)
#define INTERN_FLAGS (4)

  /* Random variables */
  char *actual_cmd = NULL, *worked_cmd = NULL;
  char *args[MAX_LINE], *chrono[MAX_CHRONO];
  size_t chrono_pos = 0;
  int flags[FLAGS];

  /* Initializing the arrays */
  init(args, MAX_LINE);
  init(chrono, MAX_CHRONO);
  while(1) {
    /* If the user entered no string */
    if (!actual_cmd) {
      /* Prints the prompt of the shell and then clean the buffer */
      printf("%s", PROMPT);
      fflush(stdout);

      actual_cmd = get_input();
    }

    if (!actual_cmd) {
      continue;
    } else {
      /* Input analisys */
      if ((worked_cmd = trim_str(actual_cmd, strlen(actual_cmd))) == NULL)
        continue;
      free(actual_cmd);
      actual_cmd = del_double(worked_cmd, strlen(worked_cmd));
      free(worked_cmd);
      worked_cmd = NULL;

      /* Tokenizing */
      flags[ARGS_FLAGS] = split_tokens(actual_cmd, args);
      EXIT_IF(error_handler(errs, errs_len, flags[ARGS_FLAGS], stderr), EXIT, goto end)

      /* Grammatical and lexical check */
      flags[CODE_FLAGS] = check_code(args, cmds, MAX_LINE, cmds_len);
      EXIT_IF(error_handler(errs, errs_len, flags[CODE_FLAGS], stderr), EXIT, goto end)

      /* Execution and Pushing on the history */
      if (flags[CODE_FLAGS] == NO_ERRORS) {
        /* Save the command in the history */
        flags[PUSH_FLAGS] = push(chrono, actual_cmd, chrono_pos++);
        EXIT_IF(error_handler(errs, errs_len, flags[PUSH_FLAGS], stderr), EXIT, goto end)

        /* Executes all the other bash commands */
        flags[EXTERN_FLAGS] = extern_code(args, MAX_LINE, errs, errs_len);
        EXIT_IF(error_handler(errs, errs_len, flags[EXTERN_FLAGS], stderr), EXIT, goto end)
      } else {
        /* Executes all the internal commands */
        flags[INTERN_FLAGS] = intern_code(errs, errs_len, args, MAX_LINE, flags[CODE_FLAGS], chrono, chrono_pos);
        EXIT_IF(error_handler(errs, errs_len, flags[INTERN_FLAGS], stderr), EXIT, goto end)
          
        if (!flags[INTERN_FLAGS])
          goto end;
        else if (flags[INTERN_FLAGS] > 0) {
          FREE_IF(actual_cmd);
          /* DO NOT FREE 
           * its' a pointer to the chronology
           */
          worked_cmd = get(chrono, chrono_pos, chrono_pos - flags[INTERN_FLAGS]);

          /* Testing if the get function worked */
          if (worked_cmd) {
            if (actual_cmd = copy(worked_cmd, strlen(worked_cmd))) {
              /* Freeing the memory */
              worked_cmd = NULL;
              free_arr_stack_p((void **)args, MAX_LINE);

              continue;
            }
          } else
            error_handler(errs, errs_len, GENERAL_ERROR, stderr);

          actual_cmd = NULL;
        } else
          ++chrono_pos;

      }

      FREE_IF(actual_cmd)
      actual_cmd = NULL;
    }

    free_arr_stack_p((void **)args, MAX_LINE);
  }

end:
  /* Resetting every pointer */
  free_arr_stack_p((void **)args, MAX_LINE);
  free_arr_stack_p((void **)chrono, MAX_CHRONO);
  FREE_IF(actual_cmd)
  FREE_IF(worked_cmd)

  return 0;
}

#define BUF_SIZ 500
#define END_LINE '\n'
#define END_STR '\0'

/* This function gets a valid input */
char *get_input(void) {
  char *buffer = malloc(sizeof(char)*BUF_SIZ);
  size_t index = 0;

  /* Get a string in input */
  while(index < BUF_SIZ-1 && (buffer[index] = getchar()) != END_LINE)
    index++;

  /* Set the last character */
  buffer[index] = END_STR;

  if (index == 0) {
    FREE_IF(buffer);
    return NULL;
  }
  
  return realloc(buffer, index);
}

#define DEF_SIZ 128
#define DEF_TIMES 2

/*
 * Function used to delete the special characters at the
 * start and at the end of the string
 *
 * @param const char *cmd -> the string to elaborate
 * @param size_t len      -> the length of the string
 *
 * @return the new string
 */
char *trim_str(const char *cmd, size_t len) {
  char *new_cmd = NULL;
  size_t new_length = len, start_index = 0, end_index = len-1; /* size_t len must not indicate the '\0' character */

  /* Skip the spaces at the start of the string */
  while(cmd[start_index] == ' ')
    start_index++;

  /* Skip the spaces at the end of the string */
  while(cmd[end_index]  == ' ')
    end_index--;
  ++new_length; /* to allow the '\0' */
  new_length = (end_index-start_index)+1;

  new_cmd = malloc(sizeof(char)*new_length);
  if (!new_cmd)
    return NULL;

  if (!memcpy(new_cmd, cmd+start_index, new_length))
    return NULL;

  new_cmd[new_length] = '\0';
  return realloc(new_cmd, new_length);
}

/*
 * Function that will delete all the consequential and multiple special 
 * characters in the string
 *
 * @param const char *cmd -> the string to elaborate
 * @param size_t len      -> the length of the string
 *
 * @return the new string
 */
char *del_double(const char *cmd, size_t len) {
  char *new_cmd = malloc(sizeof(char)*(len+1));
  size_t n_length = len, index = 0, second_index = 0;
  int last_char_spec = 0; /* 0 if the last character was not a special character */

  while(cmd[index]) {
    if ((cmd[index] == ' ' || cmd[index] == '\n') && !last_char_spec) {
      last_char_spec = 1;
      new_cmd[second_index++] = cmd[index++];
    } else if (isprint(cmd[index])) {
      last_char_spec = 0;
      new_cmd[second_index++] = cmd[index++];
    } else
      index++;
  }

  new_cmd[second_index] = '\0';
  return realloc(new_cmd, second_index);
}

/*
 * Used to split the commands
 * 
 * @param const char *cmd    -> the command inserted by the user
 * @param const char *args[] -> the array used to store the args
 *
 * @return a return signal used for signals like errors
 */
int split_tokens(const char *cmd, char *args[]) {
  char buffer_key[DEF_SIZ] = { 0 };
  size_t i = 0, j = 0, k = 0;
  size_t len;

  if (!cmd)
    return INPUT_ERROR;

  len = strlen(cmd)+1;
  while(k < len && i < DEF_SIZ) {
    if (cmd[k] == ' ' || cmd[k] == END_LINE
                      || cmd[k] == END_STR) {
      buffer_key[i] = END_STR;
      if (push(args, buffer_key, j++) != NO_ERRORS)
        return MEM_ERROR;

      i = 0;
    } else
      buffer_key[i++] = cmd[k];

    k++;
  }

  return NO_ERRORS;
}

/*
 * Function that takes a string and return a new copy of it
 *
 * @param const char *str -> the string to copy
 * @param size_t len      -> the length of the string
 *
 * @return the string copied
 */
static char *copy(const char *str, size_t len) {
  char *n_str;

  if (str && (n_str = malloc(sizeof(char)*(len+1))) && memcpy(n_str, str, len+1))
    return n_str;
  
  return NULL;
}

/*
 * The init funciton initialize all the element in the array 
 *
 * @param const char *args[] -> a pointer to an array of pointers
 * @param size_t len         -> length of the pointer
 *
 * @return void
 */
static void init(char *args[], size_t len) {
  for (size_t i=0; i<len; i++)
    args[i] = NULL;
}

/*
 * Function used to push an elementon the array
 *
 * @param char *args[]      -> the list of strings
 * @param const char *token -> the new string to be added
 * @param size_t pos        -> the position at which the token should be added
 * 
 * @return a signal
 */
static int push(char *args[], const char *token, size_t pos) {
  size_t len;

  if (!args[pos]) {
    len = strlen(token)+1;
    /* Allocating the memory for the cell */
    args[pos] = malloc(sizeof(char)*len);
    memcpy((void *)args[pos], token, len);

    return NO_ERRORS;
  }

  return MEM_ERROR;
}

/*
 * Function used to delete an element from the list
 *
 * @param char *args[] -> list to work on
 * @param size_t pos   -> the position to work on
 * @param size_t len   -> the length of the list
 *
 * @return exit status
 */
static int pop(char *args[], size_t pos, size_t len) {
  if (args && args[pos]) {
    /* Deleting the content of the pop{ped} cell */
    free((void *)args[pos]);

    /* Zipping the array's content */
    for (size_t i=pos; i<(len-1) && args[i]; i++)
      args[i] = args[i+1];

    return NO_ERRORS;
  }
  
  return MEM_ERROR;
}

/*
 * Function used to print on the screen the entire array of args
 *
 * @param char *args[] -> the list 
 * @param size_t len   -> the length of the list
 *
 * @return void
 */
static void print(char *args[], size_t len) {
  size_t i=0;

  while(i<len && args[i])
    printf("args[%d] = %s\n", i, args[i++]);
}

/*
 * Function used to get a string from the list
 *
 * @param char *args[]  -> the list to work
 * @param size_t len    -> the length of the list
 * @param size_t index  -> the string to get
 *
 * @return const char * -> returns a pointer to the string we required
 */
static const char *get(char *args[], size_t len, size_t index) {
  if (index >= len)
    return NULL;

  return args[index];
}

/*
 * Returns a new pointer to a new position of the passed array
 */
static const char **new_arr(char *args[], size_t len, size_t index) {
  if (index >= len || !args)
    return NULL;

  /* Returning the pointer at  [args] offset index */
  return (const char **)(args+index);
}

/*
 * This function is used to check if the pointers of the passed array exists (at least one)
 */
static int check_content(const char *args[], size_t len) {
  int num=0;

  for (size_t i=0; i<len; i++)
    if (args[i])
      num++;

  return num;
}

/*
 * This function is used to handle errors thrown by
 * the program during the execution
 */
static int error_handler(const error_t *errs, size_t len, int err, FILE *end_file) {
  if (err >= 0)
    return NO_ERRORS;

  for (size_t i=0; i<len; i++)
    if (err == errs[i].error_code) {
      fprintf(end_file, "%s", errs[i].error_msg);
      return (errs[i].should_exit ? EXIT : NO_ERRORS);
    }

  /* If the error is not found throw a general error */
  fprintf(end_file, errs[len-1].error_msg);
  return GENERAL_ERROR;
}

/*
 * This function is used to search a cmd in the command_t array
 */
static int search_cmd(const command_t *cmds, size_t len, const char *cmd) {
  for (size_t i=0; i<len; i++)
    /* strcmp returns 0 if the two args are equal */
    if (!strcmp(cmds[i].command, cmd))
      return i;

  /* command not found, not an error it can be a bash command unknown to the osh */
  return NO_ERRORS;
}

/*
 * Checks if the command selected needs some arguments
 */
static int requires_args(const command_t *cmds, size_t len, size_t index) {
  return cmds[index].needs_args;
}

/*
 * This function is used to execute the external commands
 *
 * @param char *args[]        -> list of arguments including the command
 * @param size_t args_len     -> number of the arguments
 * @param const error_t *errs -> list of errors
 * @param size_t errs_len     -> number of errors
 * 
 * @return a signal like an error
 */
static int extern_code(char *args[], size_t args_len, const error_t *errs, size_t errs_len) {
  size_t bg;
  int flag, flag_pop;
  pid_t pid;

  if ((bg = check_background(args, args_len))) {
    flag_pop = error_handler(errs, errs_len, pop(args, bg, MAX_LINE), stderr);
    EXIT_IF(flag_pop, EXIT, return EXIT);
  }

  pid = fork();
  if (pid < 0)
    return FORK_ERROR;
  else if (pid == 0) {
    flag = execvp(args[0], args);
    EXIT_IF(flag, -1, return CMD_NOT_FOUND)
  } else {
    if (bg) {
      printf(" [+] %d", pid);
      wait(NULL);
    }
  }

  return NO_ERRORS;
}

/*
 * Function used to execute the internal commands of the osh like
 * [ 'history', 'exit', '!!', '! n' ]
 *
 * @param const error_t *errs -> the list of possible errors
 * @param size_t errs_len     -> the length of the list of errors
 * @param char *args[]        -> the list of arguments
 * @param size_t args_len     -> the length of the arguments's list
 * @param int code_flag       -> the code flag
 * @param char *chrono[]      -> the history of commands
 * @param size_t chrono_pos   -> the next free position in the history list
 *
 * @return int
 */
static int intern_code(const error_t *errs, size_t errs_len, 
    char *args[], size_t args_len, int code_flag, char *chrono[], size_t chrono_pos) {
  size_t exit_flag, num;

  switch(code_flag) {
  case 0:
    return 0;
  case 1:
    /* Pushing the last command onto the history */
    exit_flag = error_handler(errs, errs_len, push(chrono, args[0], chrono_pos++), stderr);
    EXIT_IF(exit_flag, EXIT, return EXIT);

    /* Printing the history */
    history(chrono, chrono_pos-1, 10);
    break;
  case 2:
    return 1;
  case 3:
    if (!(num = atoi(args[1])))
      return INVALID_ARGUMENT;

    return num;
  default:
    break;
  }

  return NO_ERRORS;
}

/*
 * This function checks if the code has all the arguments it needs and if it is 
 * an internal commands.
 *
 * @param char *args[]          -> list of arguments
 * @param const command_t *cmds -> the list of commands
 * @param size_t len_args       -> the size of the arguments
 * @param size_t len_cmds       -> the length of the list of cmds
 * 
 * @return int
 */
int check_code(char *args[], const command_t *cmds, size_t len_args, size_t len_cmds) {
  if (args) {
    /* Getting the shell command and the parameters */
    const char *cmd = get(args, len_args, 0);
    const char **parameters = new_arr(args, len_args, 1);
    int cmd_flag;

    /* cmd should exists after che input checking */
    if ((cmd_flag = search_cmd(cmds, len_cmds, cmd)) >= 0) {
      if (requires_args(cmds, len_cmds, cmd_flag)) { 
        if (check_content(parameters, len_args))
          return cmd_flag;

        return INVALID_ARGUMENT;
      }
      return cmd_flag;
    } else
      return NO_ERRORS;
  }

  return INVALID_ARGUMENT;
}

/*
 * This function prints the first 'lim' valus from the end of the queue
 * so chrono_len must be the last non-NULL element.
 *
 * @param const char *chrono[] -> an array of char *
 * @param size_t chrono_len    -> the last non-NULL element
 * @param size_t lim           -> how many arguments
 *
 * @return void
 */
void history(char *chrono[], size_t chrono_len, size_t lim) {
  size_t max, j=0;

  max = (chrono_len <= lim) ? 0 : (chrono_len-lim);

  for (size_t i=chrono_len; i>=max && chrono[i]; i--)
    printf("%zu %s\n", j++, chrono[i]);
}

/*
 * Function used to understand if the command needs to be executed in backgruond
 *
 * @param char *args[] -> the list of args
 * @param size_t len   -> the length of arguments
 *
 * @return int
 */
static int check_background(char *args[], size_t len) {
  size_t i = 0;
  while(i < len && args[i])
    i++;

  return (args[i-1][0] == '&' ? (i-1) : 0);
}

/*
 * Used to free an array of pointers
 *
 * @param void *to_free[] -> the list to be freed
 * @param size_t size     -> the size of the list
 *
 * @return void
 */
void free_arr_stack_p(void *to_free[], size_t size) {
  if (!to_free)
    return;

  for (size_t i=0; i<size; i++)
    if (to_free[i]) {
      free((void *)to_free[i]);
      to_free[i] = NULL;
    }
}
