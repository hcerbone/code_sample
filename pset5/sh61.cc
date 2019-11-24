#include "sh61.hh"
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <sys/wait.h>
#include <vector>

// struct command
//    Data structure describing a command. Add your own stuff.
struct command {
  std::vector<std::string> args;
  command *next_cmd;
  pid_t pid; // process ID running this command, -1 if none
  command();
  ~command();
  pid_t make_child(pid_t pgid, int in_fd, int out_fd, int close_fd);
};
// command::command()
//    This constructor function initializes a `command` structure. You may
//    add stuff to it as you grow the command structure.

command::command() {
  this->pid = -1;
  this->next_cmd = nullptr;
}

// command::~command()
//    This destructor function is called to delete a command.

command::~command() { delete this->next_cmd; }

struct pipeline {
  command *first_command;
  pipeline *next_pipeline;
  bool is_or;
  bool is_and;

  pipeline();
  ~pipeline();
  pid_t run_commands();
};

pipeline::pipeline() {
  this->next_pipeline = nullptr;
  this->first_command = nullptr;
  this->is_or = false;
  this->is_and = false;
}
pipeline::~pipeline() {
  delete this->first_command;
  delete this->next_pipeline;
}
struct chain {
  chain *next_chain;
  pipeline *first_pipeline;
  bool is_background;
  chain();
  ~chain();
  void run_pipelines();
};
chain::chain() {
  this->is_background = false;
  this->next_chain = nullptr;
  this->first_pipeline = nullptr;
}
chain::~chain() {
  delete this->first_pipeline;
  delete this->next_chain;
}

// COMMAND EXECUTION

// command::make_child(pgid)
//    Create a single child process running the command in `this`.
//    Sets `this->pid` to the pid of the child process and returns `this->pid`.
//
//    PART 1: Fork a child process and run the command using `execvp`.
//       This will require creating an array of `char*` arguments using
//       `this->args[N].c_str()`.
//    PART 5: Set up a pipeline if appropriate. This may require creating a
//       new pipe (`pipe` system call), and/or replacing the child process's
//       standard input/output with parts of the pipe (`dup2` and `close`).
//       Draw pictures!
//    PART 7: Handle redirections.
//    PART 8: The child process should be in the process group `pgid`, or
//       its own process group (if `pgid == 0`). To avoid race conditions,
//       this will require TWO calls to `setpgid`.

pid_t command::make_child(pid_t pgid, int in_fd, int out_fd, int close_fd) {
  if (this->args.size() == 0) {
    _exit(0);
  }
  (void)pgid; // You won’t need `pgid` until part 8.

  pid_t child_id = fork();
  assert(child_id >= 0);

  // child
  if (child_id == 0) {
    if (close_fd != 0) {
      close(close_fd);
    }
    if (in_fd != -1) {
      dup2(in_fd, STDIN_FILENO);
      close(in_fd);
    }
    if (out_fd != -1) {
      dup2(out_fd, STDOUT_FILENO);
      close(out_fd);
    }
    const char *in_args[this->args.size() + 1];
    for (size_t i = 0; i < this->args.size(); ++i) {
      in_args[i] = this->args[i].c_str();
    }
    in_args[this->args.size()] = nullptr;
    execvp(in_args[0], (char **)in_args);
    _exit(1);

  } else { // parent
    this->pid = child_id;
    return this->pid;
  }
}

// run(c)
//    Run the command *list* starting at `c`. Initially this just calls
//    `make_child` and `waitpid`; you’ll extend it to handle command lists,
//    conditionals, and pipelines.
//
//    PART 1: Start the single command `c` with `c->make_child(0)`,
//        and wait for it to finish using `waitpid`.
//    The remaining parts may require that you change `struct command`
//    (e.g., to track whether a command is in the background)
//    and write code in `run` (or in helper functions).
//    PART 2: Treat background commands differently.
//    PART 3: Introduce a loop to run all commands in the list.
//    PART 4: Change the loop to handle conditionals.
//    PART 5: Change the loop to handle pipelines. Start all processes in
//       the pipeline in parallel. The status of a pipeline is the status of
//       its LAST command.
//    PART 8: - Choose a process group for each pipeline and pass it to
//         `make_child`.
//       - Call `claim_foreground(pgid)` before waiting for the pipeline.
//       - Call `claim_foreground(0)` once the pipeline is complete.
pid_t pipeline::run_commands() {
  int last_pid = -1;
  command *curr_cmd = this->first_command;

  int curr_stdin = -1;
  int curr_stdout = -1;
  int pfd[2] = {-1, -1};

  while (curr_cmd != nullptr) {
    if (curr_cmd->next_cmd == nullptr) {
      pfd[0] = -1;
      curr_stdout = -1;
    } else {
      int check = pipe(pfd);
      assert(check == 0);
      curr_stdout = pfd[1];
    }
    last_pid = curr_cmd->make_child(0, curr_stdin, curr_stdout, pfd[0]);
    if (curr_stdin != -1) {
      close(curr_stdin);
    }
    if (curr_stdout != -1) {
      close(curr_stdout);
    }
    curr_stdin = pfd[0];
    curr_cmd = curr_cmd->next_cmd;
  }
  return last_pid;
}
void chain::run_pipelines() {
  pipeline *curr_pipeline = this->first_pipeline;
  int status;
  while (curr_pipeline != nullptr) {
    pid_t last_pid = curr_pipeline->run_commands();
    pid_t exited_pid = waitpid(last_pid, &status, 0);
    assert(last_pid == exited_pid);
    if (WIFEXITED(status)) {
      if (WEXITSTATUS(status) == 0) {
        while (curr_pipeline != nullptr && curr_pipeline->is_or) {
          curr_pipeline = curr_pipeline->next_pipeline;
        }
        if (curr_pipeline != nullptr) {
          curr_pipeline = curr_pipeline->next_pipeline;
        }
      } else {
        while (curr_pipeline != nullptr && curr_pipeline->is_and) {
          curr_pipeline = curr_pipeline->next_pipeline;
        }
        if (curr_pipeline != nullptr) {
          curr_pipeline = curr_pipeline->next_pipeline;
        }
      }
    } else {
      curr_pipeline = nullptr;
    }
  }
}

void run(chain *curr_chain) {
  while (curr_chain != nullptr) {
    if (curr_chain->is_background) {
      pid_t child_pid = fork();
      assert(child_pid >= 0);
      if (child_pid == 0) {
        curr_chain->run_pipelines();
        _exit(0);
      }
    } else {
      curr_chain->run_pipelines();
    }
    curr_chain = curr_chain->next_chain;
  }
}

// parse_line(s)
//    Parse the command list in `s` and return it. Returns `nullptr` if
//    `s` is empty (only spaces). You’ll extend it to handle more token
//    types.

chain *parse_line(const char *s) {
  int type;
  std::string token;
  // Your code here!

  // build the command
  // (The handout code treats every token as a normal command word.
  // You'll add code to handle operators.)
  chain *c = nullptr;
  command *curr_cmd = nullptr;
  chain *curr_chain = nullptr;
  pipeline *curr_pipeline = nullptr;
  bool new_chain = false;
  bool new_pipeline = false;
  while ((s = parse_shell_token(s, &type, &token)) != nullptr) {
    if (!c) {
      c = new chain;
      c->first_pipeline = new pipeline;
      curr_pipeline = c->first_pipeline;
      curr_pipeline->first_command = new command;
      curr_cmd = curr_pipeline->first_command;
      curr_chain = c;
    }
    if (new_chain) {
      new_chain = false;
      // fprintf(stderr, "HERE\n");
      curr_chain->next_chain = new chain;
      curr_chain = curr_chain->next_chain;
      curr_chain->first_pipeline = new pipeline;
      curr_pipeline = curr_chain->first_pipeline;
      curr_pipeline->first_command = new command;
      curr_cmd = curr_pipeline->first_command;
    }
    if (new_pipeline) {
      new_pipeline = false;
      curr_pipeline->next_pipeline = new pipeline;
      curr_pipeline = curr_pipeline->next_pipeline;
      curr_pipeline->first_command = new command;
      curr_cmd = curr_pipeline->first_command;
    }
    switch (type) {
    case TYPE_NORMAL:
      curr_cmd->args.push_back(token);
      break;
    case TYPE_SEQUENCE:
      curr_chain->is_background = false;
      new_chain = true;
      break;
    case TYPE_BACKGROUND:
      curr_chain->is_background = true;
      new_chain = true;
      break;
    case TYPE_OR:
      curr_pipeline->is_or = true;
      new_pipeline = true;
      break;
    case TYPE_AND:
      curr_pipeline->is_and = true;
      new_pipeline = true;
      break;
    case TYPE_PIPE:
      curr_cmd->next_cmd = new command;
      curr_cmd = curr_cmd->next_cmd;
      break;
    default:
      curr_cmd->args.push_back(token);
    }
  }
  return c;
}

int main(int argc, char *argv[]) {
  FILE *command_file = stdin;
  bool quiet = false;

  // Check for '-q' option: be quiet (print no prompts)
  if (argc > 1 && strcmp(argv[1], "-q") == 0) {
    quiet = true;
    --argc, ++argv;
  }

  // Check for filename option: read commands from file
  if (argc > 1) {
    command_file = fopen(argv[1], "rb");
    if (!command_file) {
      perror(argv[1]);
      exit(1);
    }
  }

  // - Put the shell into the foreground
  // - Ignore the SIGTTOU signal, which is sent when the shell is put back
  //   into the foreground
  claim_foreground(0);
  set_signal_handler(SIGTTOU, SIG_IGN);

  char buf[BUFSIZ];
  int bufpos = 0;
  bool needprompt = true;

  while (!feof(command_file)) {
    // Print the prompt at the beginning of the line
    if (needprompt && !quiet) {
      printf("sh61[%d]$ ", getpid());
      fflush(stdout);
      needprompt = false;
    }

    // Read a string, checking for error or EOF
    if (fgets(&buf[bufpos], BUFSIZ - bufpos, command_file) == nullptr) {
      if (ferror(command_file) && errno == EINTR) {
        // ignore EINTR errors
        clearerr(command_file);
        buf[bufpos] = 0;
      } else {
        if (ferror(command_file)) {
          perror("sh61");
        }
        break;
      }
    }

    // If a complete command line has been provided, run it
    bufpos = strlen(buf);
    if (bufpos == BUFSIZ - 1 || (bufpos > 0 && buf[bufpos - 1] == '\n')) {
      if (chain *c = parse_line(buf)) {
        run(c);
        delete c;
      }
      bufpos = 0;
      needprompt = 1;
    }

    // Handle zombie processes and/or interrupt requests
    // Your code here!
  }

  return 0;
}
