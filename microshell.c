#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

//string manipulations
int str_equal(char *str1, char *str2)
{
    return str1 != NULL && str2 != NULL && strcmp(str1, str2) == 0;
}
int my_strlen(char *str)
{
    int i;
    for (i = 0; i < str[i]; i++) ;
    return i;
}

//errors handling
void write_error_endl(char *err_msg)
{
    write(2, err_msg, my_strlen(err_msg));
    write(2, "\n", 1);
}
void write_two_err_msgs(char *err_msg1, char *err_msg2)
{
    write(2, err_msg1, my_strlen(err_msg1));
    write(2, err_msg2, my_strlen(err_msg2));
    write(2, "\n", 1);
}
void exit_with_fatal_error()
{
    write_error_endl("error: fatal");
    exit(1);
}

//execution
int my_cd(int ac, char **av)
{
    if (ac != 2)
    {
        write_error_endl("error: cd: bad arguments");
        return 1;
    }
    else if (chdir(av[1]) == -1)
    {
        write_two_err_msgs("error: cd: cannot change directory to ", av[1]);
        return 1;
    }
    return 0;
}
void close_pipes(int prev_pipe_read, int current_pipe_read, int current_pipe_write)
{
    if (prev_pipe_read != -1)
        close(prev_pipe_read);
    if (current_pipe_read != -1)
        close(current_pipe_read);
    if (current_pipe_write != -1)
        close(current_pipe_write);
}
int my_dup(int src_fd, int dst_fd)
{
    if (src_fd > 2 && dup2(src_fd, dst_fd) == -1)
    {
// printf("    fatal error in my_dup\n");
        return -1;
    }
    return 0;
// printf("    NO fatal error in my_dup src_fd = %d, dst_fd = %d\n", src_fd, dst_fd);
}
//returns pid of child or -1 if sys call failed.
//We don't exit just yet if a sys call failed because we are going to need to close all the previous pipe fds.
int exe_cmd_in_child(int ac, char **av, char **env, int prev_pipe_read, int current_pipe_read, int current_pipe_write)
{
    pid_t child_pid = fork();
    if (child_pid == 0)//we are children
    {
        // printf("\n    GOING to execute \"");
        // for (int i = 0; i < ac; i++)
        //     printf("%s ", av[i]);
        // printf("\"\n");
        // printf("    pipe_read = %d, pipe_write = %d\n", prev_pipe_read, current_pipe_write);
        my_dup(prev_pipe_read, 0);
        my_dup(current_pipe_write, 1);
        // printf("    pipe write dup done\n");
        close_pipes(prev_pipe_read,  current_pipe_read, current_pipe_write);
        av[ac] = NULL;
        if (str_equal(*av, "cd"))
            exit(my_cd(ac, av));
        else 
        {
            // printf("    executing \"");
            // for (int i = 0; i < ac; i++)
            //     printf("%s ", av[i]);
            // printf("\"\n\n");

            if (execve(*av, av, env) == -1)
            {
                write_two_err_msgs("error: cannot execute ", *av);
                exit(1);
            }
        }
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
    // printf("\nparse_and_exe_before_pipe called with \"");
    // for (int i = 0; i < exe_ac; i++)
    //     printf(" %s", av[i]);
    // printf("\"\n");

    int pipe_fds[2];
    if (pipe(pipe_fds) == -1)
        return -1;
    pid_t child_pid = exe_cmd_in_child(exe_ac, av, env, prev_pipe_read_fd, *pipe_fds, pipe_fds[1]);
    // printf("child_pid in parent = %d, av[0] =\"%s\"\n", child_pid, av[0]);
    int ret = 0;
    if ((prev_pipe_read_fd != -1 && close(prev_pipe_read_fd)) | (close(pipe_fds[1]) == -1))
        ret = -1;
    if (ret != -1)
        ret = parse_and_exe_pipeline(av + exe_ac + 1, env, *pipe_fds, last_exit_status_dst);
    if (child_pid != -1)
    {
        // printf("waiting for child = %d, av[0] = \"%s\"\n", child_pid, av[0]);
        ret = wait_for_child(child_pid);
    }
    // printf("ret = %d after waiting for child = %d, av[0] =\"%s\"\n", ret, child_pid, av[0]);
    return ret;
}
//prev_pipe_read, int current_pipe_read, int current_pipe_write
//We just execute the last executable and close prev_pipe_read without calling exe_pipeline or creating a new pipe again, effectively ending the recursion.
int parse_and_exe_end_of_pipeline(int exe_ac, char **av, char **env, int prev_pipe_read_fd, int *last_exit_status_dst)
{
    // printf("\nparse_and_exe_END_of_pipeline called with \"");
    // for (int i = 0; i < exe_ac; i++)
    //     printf("%s ", av[i]);
    // printf("\"\n");

    //edge case for cd not being in a pipeline
    if (str_equal(*av, "cd") && prev_pipe_read_fd == -1)
    {
        *last_exit_status_dst = my_cd(exe_ac, av);
        return *last_exit_status_dst;
    }
    int child_pid = exe_cmd_in_child(exe_ac, av, env, prev_pipe_read_fd, -1, -1);
    int ret = -1;
    if (child_pid != -1)
    {
        // printf("waiting for LAST child = %d, av[0] = \"%s\"\n", child_pid, av[0]);
        ret = wait_for_child(child_pid);
    }
    if ((prev_pipe_read_fd != -1 && close(prev_pipe_read_fd)))
        return -1;
    *last_exit_status_dst = ret;
    return ret;
}
//prev_out_fd = -1 when there was no previous out_fd(i.e this is the first exe_pipeline call)
//returns -1 if sys call failed to allow the previous iterations to close their pipe fds
int parse_and_exe_pipeline(char **av, char **env, int prev_pipe_read_fd, int *last_exit_status_dst)
{
    //measure ac of the executable we are going to execute
    int exe_ac = 0;
    while (av[exe_ac] != NULL && !str_equal(av[exe_ac], "|") && !str_equal(av[exe_ac], ";"))
        exe_ac++;
    //if i == 0 then av[0] can only be NULL, not "|" equal to ";" because the subject guarantees that microshell won't be called with "unclosed" pipelines.
    //So this means there is no pipeline, in which case, we just return 0 and set last_exit_status_dst to 0.
    if (exe_ac == 0)
    {
        *last_exit_status_dst = 0;
        return 0;
    }
    //if we are herem this means that we are in a pipeline.
    //Check if there is a "|", if so, we need to create anoter pipe.
    if (str_equal(av[exe_ac], "|"))
        return parse_and_exe_before_pipe(exe_ac, av, env, prev_pipe_read_fd, last_exit_status_dst);
    //If we are here, this means that we are at the end of the pipeline(because all of the previous if blocks would have returned).
    //or that there was just a command and a ";", no pipeline
    return parse_and_exe_end_of_pipeline(exe_ac, av, env, prev_pipe_read_fd, last_exit_status_dst);
}


int main(int ac, char **av, char **env)
{
    (void)ac;
    int last_exit_status_dst;
    if (parse_and_exe_pipeline(av + 1, env, -1, &last_exit_status_dst) == -1)
    {
        // printf("parse_and_exe_pipeline returned with exit status -1\n");
        exit_with_fatal_error();
    }
    // printf("parse_and_exe_pipeline returned with exit status 0\n");
    return last_exit_status_dst;
}