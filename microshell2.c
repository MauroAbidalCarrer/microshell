#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

//string handling
int str_equal(char *str1, char *str2)
{
    return str1 != NULL && str2 != NULL && strcmp(str1, str2) == 0;
}
int my_strlen(char *str)
{
    int i = 0;
    while (str[i])
        i++;
    return i;
}

//error handling
int write_error(char *msg1, char *msg2)
{
    write(2, msg1, my_strlen(msg1));
    if (msg2 != NULL)
        write(2, msg1, my_strlen(msg1));
    write(2, "\n", 1);
    return 1;
}
void exit_with_fatal_error()
{
    exit(write_error("error: fatal", NULL));
}

//execution
int my_cd(int ac, char** av)
{
    if (ac != 2)
        return write_error("error: cd: bad arguments", NULL);
    if (chdir(av[1]) == -1)
        return write_error("error: cd: cannot change directory to ", av[1]);
    return 0;
}
int exe_cmd_in_child(int ac, char **av, char **env, int prev_pipe_read, int current_pipe_read, int current_pipe_write)
{
    pid_t child_pid = fork();
    if (child_pid == 0)
    {
        int ret = 0;
        //dup pipe fds on std in and out
        if ((prev_pipe_read != -1 && dup2(prev_pipe_read, 0) == -1) | (current_pipe_write != -1 && dup2(current_pipe_write, 1) == -1))
            ret = -1;
        //close pipe fds
        if ((prev_pipe_read != -1 && close(prev_pipe_read) == -1) |
            (current_pipe_write != -1 && close(current_pipe_write) == -1) |
            (current_pipe_read != -1 && close(current_pipe_read) == -1))
            ret = -1;
        //null terminate avs
        av[ac] = NULL;
        //assert that there were no errors so far
        if (ret == -1)
            return -1;
        //try to execute the executble
        if (execve(*av, av, env))
            exit(write_error("error: cannot execute ", *av));
    }
    return child_pid;
}
int wait_for_child(pid_t child_pid)
{
    int exit_status;
    if (child_pid == -1 || waitpid(child_pid, &exit_status, 0) == -1)
        return -1;
    return exit_status;
}
int exe_pipeline(char **av, char **env, int prev_read_pipe, int *last_exit_status);
int exe_cmd_before_pipe(int ac, char **av, char **env, int prev_read_pipe, int *last_exit_status)
{
    int pipe_fds[2];
    if (pipe(pipe_fds) == -1)
        return -1;
    pid_t child_pid = exe_cmd_in_child(ac, av, env, prev_read_pipe, pipe_fds[0], pipe_fds[1]);
    int ret = 0;
    if ((prev_read_pipe != -1 && close(prev_read_pipe) == -1) | (close(pipe_fds[1]) == -1))
        ret = -1;
    if (ret != -1 && exe_pipeline(av + ac + 1, env, pipe_fds[0], last_exit_status) == -1)
        ret = -1;
    if (wait_for_child(child_pid) == -1)
        ret = -1;
    return ret;
}
int exe_cmd_with_no_pipe_after(int ac, char **av, char **env, int prev_read_pipe, int *last_exit_status)
{
    if (str_equal(*av, "cd"))
        return (*last_exit_status = my_cd(ac, av));
    pid_t child_pid = exe_cmd_in_child(ac, av, env, prev_read_pipe, -1, -1);
    int ret = 0;
    if ((prev_read_pipe != -1 && close(prev_read_pipe) == -1))
        ret = -1;
    if (wait_for_child(child_pid) == -1)
        ret = -1;
    return ret;
}
int exe_pipeline(char **av, char **env, int prev_read_pipe, int *last_exit_status)
{
    int exe_ac = 0;
    while (av[exe_ac] && !str_equal(av[exe_ac], ";") && !str_equal(av[exe_ac], "|"))
        exe_ac++;
// printf("exe_pipeline called on \"%s\", exe_ac = %d, av[exe_ac] = \"%s\"\n", *av, exe_ac, av[exe_ac]);
    if (str_equal(av[exe_ac], "|"))
        return exe_cmd_before_pipe(exe_ac, av, env, prev_read_pipe, last_exit_status);
    return exe_cmd_with_no_pipe_after(exe_ac, av, env, prev_read_pipe, last_exit_status);
}

int main(int ac, char **av ,char **env)
{
    int last_exit_status = 0;
    ac--;
    av++;
    do
    {
        if (!str_equal(*av, ";") && exe_pipeline(av, env, -1, &last_exit_status) == -1)
            exit_with_fatal_error();
        while (ac >= 0 && !str_equal(*av, ";"))
        {
            ac--;
            av++;
        }
        ac--;
        av++;
    } while (ac >= 0);
    return last_exit_status;
}