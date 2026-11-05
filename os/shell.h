/*
 * shell.h - Serial command shell, itself an ordinary task.
 *
 * Author: Skylar J. Sabin - CS 301 semester project
 */

#ifndef SHELL_H
#define SHELL_H

/* Entry point for the shell task. Pass to os_spawn(). Never returns. */
void shell_task(void *arg);

#endif /* SHELL_H */
