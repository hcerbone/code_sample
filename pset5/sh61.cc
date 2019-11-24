#include "sh61.hh"
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <sys/wait.h>
#include <vector>

volatile sig_atomic_t got_signal;

void signal_handler(int signal) {
  (void)signal;
  got_signal = -1;
}
// struct command
//    Data structure describing a command. Add your own stuff.
struct command {
  std::vector<std::string> args;
  command *next_cmd;
  pid_t pid; // process ID running this command, -1 if none
  std::string redirs[3];
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
  pid_t run_commands(bool is_background);
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

  pid_t child_pid = fork();
  assert(child_pid >= 0);

  // child
  if (child_pid == 0) {

    if (pgid == 0) {
      setpgid(0, 0);
    } else {
      setpgid(getpid(), pgid);
    }
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
    for (int i = 0; i < 3; ++i) {
      int flags = O_WRONLY | O_CREAT | O_TRUNC;
      if (i == 0) {
        flags = O_RDONLY;
      }
      if (!this->redirs[i].empty()) {
        const char *redir_str = this->redirs[i].c_str();
        int new_fd = open(redir_str, flags, 0666);
        if (new_fd == -1) {
          fprintf(stderr, "%s: %s\n", redir_str, strerror(errno));
          _exit(1);
        }
        dup2(new_fd, i);
        close(new_fd);
      }
    }
    const char *in_args[this->args.size() + 1];
    for (size_t i = 0; i < this->args.size(); ++i) {
      in_args[i] = this->args[i].c_str();
    }
    if (this->args[0] == "cd") {
      int r = chdir(this->args[1].c_str());
      _exit(r);
    }
    in_args[this->args.size()] = nullptr;
    execvp(in_args[0], (char **)in_args);
    _exit(1);
  } else { // parent
    if (pgid == 0) {
      setpgid(child_pid, child_pid);
    } else {
      setpgid(child_pid, pgid);
    }
    if (this->args[0] == "cd") {
      int z = chdir(this->args[1].c_str());
    }
    this->pid = child_pid;
    return this->pid;
  }
}

pid_t pipeline::run_commands(bool is_background) {
  int last_pid = -1;
  pid_t pgid = 0;
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
    if (pgid == 0) {
      pgid = last_pid;

      if (!is_background) {
        claim_foreground(pgid);
      }
    }
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
    pid_t last_pid = curr_pipeline->run_commands(this->is_background);
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
        _exit(1);
      }
    } else {
      curr_chain->run_pipelines();
      claim_foreground(0);
    }
    curr_chain = curr_chain->next_chain;
  }
}

// parse_line(s)
//    Parse the command list in `s` and return it. Returns `nullptr` if
//    `s` is empty (only spaces). You’ll extend it to handle more token
//    types.

chain *parse_line(const char *s) {
  bool new_chain = false;
  bool new_pipeline = false;
  bool next_file = false;
  int type;
  int redir = -1;
  std::string token;
  chain *c = nullptr;
  command *curr_cmd = nullptr;
  chain *curr_chain = nullptr;
  pipeline *curr_pipeline = nullptr;

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
      if (next_file) {
        curr_cmd->redirs[redir] = token;
        next_file = false;
      } else {
        curr_cmd->args.push_back(token);
      }
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
    case TYPE_REDIRECTION:
      if (token == ">") {
        redir = 1;
      } else if (token == "<") {
        redir = 0;
      } else if (token == "2>") {
        redir = 2;
      }
      next_file = true;
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
    pid_t wait_val = 1;
    while (wait_val > 0) {
      wait_val = waitpid(-1, 0, WNOHANG);
    }
  }

  return 0;
}
