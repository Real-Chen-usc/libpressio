#include "libpressio_ext/launch/external_launch.h"
#include <memory>
#include <sstream>
#include <unistd.h>
#include <iterator>
#include <sys/wait.h>
#include "std_compat/memory.h"

struct external_forkexec: public libpressio_launch_plugin {
extern_proc_results launch(std::vector<std::string> const& full_command) const override {
      extern_proc_results results;

      //create the pipe for stdout
      int stdout_pipe_fd[2];
      if(int ec = pipe(stdout_pipe_fd)) {
        results.return_code = ec;
        results.error_code = pipe_error;
        return results;
      }

      //create the pipe for stderr
      int stderr_pipe_fd[2];
      if(int ec = pipe(stderr_pipe_fd)) {
        results.return_code = ec;
        results.error_code = pipe_error;
        return results;
      }

      //run the program
      int child = fork();
      switch (child) {
        case -1:
          results.return_code = -1;
          results.error_code = fork_error;
          break;
        case 0:
          //in the child process
          {
          close(stdout_pipe_fd[0]);
          close(stderr_pipe_fd[0]);
          dup2(stdout_pipe_fd[1], 1);
          dup2(stderr_pipe_fd[1], 2);

          int chdir_status = chdir(workdir.c_str());
          if(chdir_status == -1) {
            perror(" failed to change to the specified directory");
            exit(-2);
          }

          std::vector<char*> args;
          for(auto const& command: commands) {
            args.push_back(const_cast<char*>(command.c_str()));
          }
          std::transform(std::begin(full_command), std::end(full_command),
              std::back_inserter(args), [](std::string const& s){return const_cast<char*>(s.c_str());});
          args.push_back(nullptr);
          if(args.front() != nullptr) {
            execvp(args.front(), args.data());
            fprintf(stdout, "external:api=5");
            perror("failed to exec process");
            //exit if there was an error
            fprintf(stderr, " %s\n", args.front());
          } else {
            fprintf(stdout, "external:api=5");
            fprintf(stderr, "no process set");
          }
          exit(-1);
          break;
          }
        default:
          //in the parent process

          //close the unused parts of pipes
          close(stdout_pipe_fd[1]);
          close(stderr_pipe_fd[1]);

          int status = 0;
          char buffer[2048];
          std::ostringstream stdout_stream;
          std::ostringstream stderr_stream;
          do {
            //read the stdout[0]
            int nread;
            while((nread = read(stdout_pipe_fd[0], buffer, 2048)) > 0) {
              stdout_stream.write(buffer, nread);
            }
            
            //read the stderr[0]
            while((nread = read(stderr_pipe_fd[0], buffer, 2048)) > 0) {
              stderr_stream.write(buffer, nread);
            }

            //wait for the child to complete
            waitpid(child, &status, 0);
          } while (not WIFEXITED(status));

          results.proc_stdout = stdout_stream.str();
          results.proc_stderr = stderr_stream.str();
          results.return_code = WEXITSTATUS(status);
      }


      return results;
    }
  const char* prefix() const override {
    return "forkexec";
  }

  int set_options(pressio_options const& options) override {
    get(options, "external:workdir", &workdir);
    get(options, "external:commands", &commands);
    return 0;
  }

  pressio_options get_options() const override {
    pressio_options options;
    set(options, "external:workdir", workdir);
    set(options, "external:commands", commands);
    return options;
  }

  
  std::unique_ptr<libpressio_launch_plugin> clone() const override {
    return compat::make_unique<external_forkexec>(*this);
  }

  std::string workdir=".";
  std::vector<std::string> commands;
};

static pressio_register launch_forkexec_plugin(launch_plugins(), "forkexec", [](){ return compat::make_unique<external_forkexec>();});
