#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

// string manipulations
int str_equal(char *str1, char *str2)
{
    return str1 != NULL && str2 != NULL && strcmp(str1, str2) == 0;
}
int my_strlen(char *str)
{
    int i;
    for (i = 0; i < str[i]; i++)
        ;
    return i;
}

// errors handling
int write_error_endl(char *err_msg)
{
    write(2, err_msg, my_strlen(err_msg));
    write(2, "\n", 1);
    return 1;
}
int write_two_err_msgs(char *err_msg1, char *err_msg2)
{
    write(2, err_msg1, my_strlen(err_msg1));
    write(2, err_msg2, my_strlen(err_msg2));
    write(2, "\n", 1);
    return 1;
}
void exit_with_fatal_error()
{
    exit(write_error_endl("error: fatal"));
}

// execution
int my_cd(int ac, char **av)
{
    if (ac != 2)
        return write_error_endl("error: cd: bad arguments");
    if (chdir(av[1]) == -1)
        return write_two_err_msgs("error: cd: cannot change directory to ", av[1]);
    return 0;
}
int exe_cmd_in_child(int ac, char **av, char **env, int prev_pipe_read, int current_pipe_read, int current_pipe_write)
{
    pid_t child_pid = fork();
    if (child_pid == 0) // we are children
    {
        int ret = 0;
        if ((prev_pipe_read > 2 && dup2(prev_pipe_read, 0) == -1) | (current_pipe_write > 2 && dup2(current_pipe_write, 1) == -1))
            ret = -1;
        if ((prev_pipe_read != -1 && close(prev_pipe_read)) |
            (current_pipe_read != -1 && close(current_pipe_read)) |
            (current_pipe_write != -1 && close(current_pipe_write)))
            ret = -1;
        av[ac] = NULL;
        if (execve(*av, av, env) == -1)
            exit(write_two_err_msgs("error: cannot execute ", *av));
    }
    return child_pid;
}
int wait_for_child(int child_pid)
{
    int exit_status = 0;
    if (waitpid(child_pid, &exit_status, 0) == -1)
        return -1;
    return exit_status;
}
int parse_and_exe_pipeline(char **av, char **env, int prev_pipe_read_fd, int *last_exit_status_dst);
int parse_and_exe_before_pipe(int exe_ac, char **av, char **env, int prev_pipe_read_fd, int *last_exit_status_dst)
{
    int pipe_fds[2];
    if (pipe(pipe_fds) == -1)
        return -1;
    pid_t child_pid = exe_cmd_in_child(exe_ac, av, env, prev_pipe_read_fd, *pipe_fds, pipe_fds[1]);
    int ret = 0;
    if ((prev_pipe_read_fd != -1 && close(prev_pipe_read_fd)) | (close(pipe_fds[1]) == -1))
        ret = -1;
    if (ret != -1)
        ret = parse_and_exe_pipeline(av + exe_ac + 1, env, *pipe_fds, last_exit_status_dst);
    if (child_pid != -1)
        ret = wait_for_child(child_pid);
    return ret;
}
int parse_and_exe_end_of_pipeline(int exe_ac, char **av, char **env, int prev_pipe_read_fd, int *last_exit_status_dst)
{
    if (str_equal(*av, "cd") && prev_pipe_read_fd == -1)
    {
        *last_exit_status_dst = my_cd(exe_ac, av);
        return *last_exit_status_dst;
    }
    int child_pid = exe_cmd_in_child(exe_ac, av, env, prev_pipe_read_fd, -1, -1);
    int ret = -1;
    if (child_pid != -1)
        ret = wait_for_child(child_pid);
    if ((prev_pipe_read_fd != -1 && close(prev_pipe_read_fd)))
        return -1;
    *last_exit_status_dst = ret;
    return ret;
}
int parse_and_exe_pipeline(char **av, char **env, int prev_pipe_read_fd, int *last_exit_status_dst)
{
    int exe_ac = 0;
    while (av[exe_ac] != NULL && !str_equal(av[exe_ac], "|") && !str_equal(av[exe_ac], ";"))
        exe_ac++;
    if (str_equal(av[exe_ac], "|"))
        return parse_and_exe_before_pipe(exe_ac, av, env, prev_pipe_read_fd, last_exit_status_dst);
    return parse_and_exe_end_of_pipeline(exe_ac, av, env, prev_pipe_read_fd, last_exit_status_dst);
}

int main(int ac, char **av, char **env)
{
    int last_exit_status_dst = 0;
    ac--;
    av++;
    do
    {
        if (*av != NULL && !str_equal(*av, ";") && parse_and_exe_pipeline(av, env, -1, &last_exit_status_dst) == -1)
            exit_with_fatal_error();
        while (*av != NULL && !str_equal(*av, ";"))
        {
            ac--;
            av++;
        }
        ac--;
        av++;
    } while (ac >= 0);
    return last_exit_status_dst;
}