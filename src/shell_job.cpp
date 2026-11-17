#include "shell_job.hpp"

#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <string>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

extern char** environ;

ShellJob::ShellJob(std::string id, std::string command, int max_attempts)
    : Job(std::move(id), max_attempts), command_(std::move(command)) {}

ExecResult ShellJob::execute() {
    char shell_path[] = "/bin/sh";
    char option[] = "-c";
    char* arguments[] = {shell_path, option, command_.data(), nullptr};
    pid_t child_pid = 0;
    const int spawn_error = ::posix_spawn(
        &child_pid, shell_path, nullptr, nullptr, arguments, environ);
    if (spawn_error != 0) {
        return ExecResult{
            Job_Status::FAILED,
            -1,
            "posix_spawn failed: " +
                std::error_code(spawn_error, std::generic_category()).message()};
    }

    int status = 0;
    pid_t waited_pid = 0;
    do {
        waited_pid = ::waitpid(child_pid, &status, 0);
    } while (waited_pid == -1 && errno == EINTR);

    if (waited_pid == -1) {
        const int wait_error = errno;
        return ExecResult{
            Job_Status::FAILED,
            -1,
            "waitpid failed: " + std::string(std::strerror(wait_error))};
    }

    if (WIFEXITED(status)) {
        const int exit_code = WEXITSTATUS(status);
        return ExecResult{
            exit_code == 0 ? Job_Status::SUCCESS : Job_Status::FAILED,
            exit_code,
            exit_code == 0
                ? std::string{}
                : "Command exited with status " + std::to_string(exit_code)};
    }

    if (WIFSIGNALED(status)) {
        const int signal_number = WTERMSIG(status);
        return ExecResult{
            Job_Status::FAILED,
            128 + signal_number,
            "Command terminated by signal " + std::to_string(signal_number)};
    }

    return ExecResult{Job_Status::FAILED, -1, "Command ended in an unexpected state"};
}
