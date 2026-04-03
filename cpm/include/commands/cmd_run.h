/*
 * cpm — Run Script Command (with nodemon-like watch support)
 */

#ifndef CPM_CMD_RUN_H
#define CPM_CMD_RUN_H

typedef struct {
    int   watch;           /* -w / --watch : re-run on file change   */
    const char *ext;       /* --ext=c,h    : file extensions to watch */
    int   delay_ms;        /* --delay=500  : debounce delay in ms     */
    int   verbose;         /* -v / --verbose : print changed file     */
    int   clear;           /* --clear      : clear terminal on restart */
    int   once;            /* --once       : run once then exit (default without -w) */
} run_opts_t;

int cmd_run(const char *script, run_opts_t *opts);

#endif /* CPM_CMD_RUN_H */
