// 
// tsh - A tiny shell program with job control
// 
// Pradyumna Kamat : pkamat
//

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>
#include <cstdlib>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"


//
// Needed global variable definitions
//
char prompt[] = "tsh> ";
int verbose = 0;
sigset_t sig_main;


//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
// 
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);


//
// main - The shell's main routine 
//
int main(int argc, char **argv) 
{
    int emit_prompt = 1; // emit prompt (default)
    
    sigemptyset(&sig_main);
    sigaddset(&sig_main, SIGCHLD);
    sigaddset(&sig_main, SIGTSTP);
    
  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
    dup2(1, 2);

    /* Parse the command line */
    char c;
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             // print help message
            usage();
	    break;
        case 'v':             // emit additional diagnostic info
            verbose = 1;
	    break;
        case 'p':             // don't print a prompt
            emit_prompt = 0;  // handy for automatic testing
	    break;
	default:
            usage();
		}
    }

  //
  // Install the signal handlers
  //
    Signal(SIGINT,  sigint_handler);   // ctrl-c
    Signal(SIGTSTP, sigtstp_handler);   // ctrl-z
    Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
    Signal(SIGQUIT, sigquit_handler); 

  //
  // Initialize the job list
  //
    initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }
    
    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

    exit(0); //control never reaches here
}
  
  
/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
// 
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline)
{
  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
  char *argv[MAXARGS];
    
  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  int bg = parseline(cmdline, argv); 

    int pid=-1;	//error case

    if(builtin_cmd(argv))
        return;   /* ignore empty lines */

    sigprocmask(SIG_BLOCK,&sig_main,NULL);
    if(bg)    //BG process start
    {
        pid=fork();
        struct job_t* job=getjobpid(jobs,pid);
        
        if(pid==0)  //child
        {
            setpgid(0,0);	//set process group
            
            sigprocmask(SIG_UNBLOCK,&sig_main,NULL);
     
            Signal(SIGINT,SIG_DFL);     //default SIGINT handler
            Signal(SIGTSTP,SIG_DFL);    //default SIGSTP handler

            if(execv(argv[0],argv)==-1)
            {
                    printf("%s: Command not found\n",argv[0]);
                    exit(0);
            }    
       
        return;
        }
        
        addjob(jobs,pid,BG,cmdline);
        printf("[%d] (%d) ", job->jid, job->pid);
	    printf("%s", job->cmdline);
        
        sigprocmask(SIG_UNBLOCK,&sig_main,NULL); 
    }
    
    else    //FG process start
    {
        pid=fork();
        
        if(pid==0)	//child
        {
            setpgid(0,0);	//set process group
            
            sigprocmask(SIG_UNBLOCK,&sig_main,NULL);
            
            signal(SIGTSTP,SIG_DFL);        //default SIGINT handler
            signal(SIGINT,SIG_DFL);         //default SIGSTP handler
            
            if(execv(argv[0],argv)==-1)
            {               
                printf("%s: Command not found\n",argv[0]);
                exit(0);
            }
            
        }
        
        addjob(jobs,pid,FG,cmdline);
        
        sigprocmask(SIG_UNBLOCK,&sig_main,NULL); 
        waitfg(pid);
        
        return;
    }
    return;
}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need 
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv)
{
    if(strcmp(argv[0],"exit")==0)
        exit(EXIT_SUCCESS);
        
    else if(strcmp(argv[0],"fg")==0||strcmp(argv[0],"bg")==0)
    {
        do_bgfg(argv);
        return 1;
    }
    
    else if(strcmp(argv[0],"jobs")==0)
    { 
        listjobs(jobs);
        return 1;
    }
    
    return 0;     /* not a builtin command */
}


int checkFG(char **argv)
{
     int i=0;
           
        if (argv[1][0]=='%')
            i++;
            
        for(;i<(int)strlen(argv[1]);i++)
        {
            if(!isdigit(argv[1][i]))
            {
				printf("fg: argument must be a PID or %%jobid\n");
                return 0;
			}                
		}
		
    return 1;
}

int checkBG(char **argv)
{
     int i=0;
     
        if (argv[1][0]=='%')
            i++;
            
        for(;i<(int)strlen(argv[1]);i++)
        {
            if(!isdigit(argv[1][i]))
            {
                printf("bg: argument must be a PID or %%jobid\n");
                return 0;
            }                
        }
        
    return 1;
}


/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv)
{
    struct job_t* job=NULL;   
    
	/* Ignore command if no argument */
	if (argv[1] == NULL) {
		printf("%s command requires PID or %%jobid argument\n", argv[0]);
		return;
	}
    
    if(strcmp(argv[0],"fg")==0)
    {
        //validate input
        if(!checkFG(argv))
            return;
               

        if (argv[1][0]=='%')
        {
            argv[1][0]='0';
            int jid=atoi(argv[1]);

            job=getjobjid(jobs,jid);
            
            if(jid<1||job==NULL)
            {
                printf("%%%d: No such job\n",jid);
                return;
            }
            
            if(job->state==ST)
            {
                kill(-(job->pid),SIGCONT);
                job->state=FG;
                waitfg(job->pid);
                
            }
            
            else if(job->state==BG)
            {
                kill(-job->pid,SIGCONT);    
                job->state=FG;
                waitfg(job->pid);            
            }
            
            return;
        }
        
        else
        {
            int pid=atoi(argv[1]);

            job=getjobpid(jobs,pid);
            
            if(pid<1||job==NULL)
            {
                printf("(%d): No such process\n",pid);
                return;
            }
            
            if(job->state==ST)
            {
                kill(-job->pid,SIGCONT);
                job->state=FG;
                waitfg(job->pid);
                
            }
            
            else if(job->state==BG)
            {
                kill(-job->pid,SIGCONT);    
                job->state=FG;
                waitfg(job->pid);
            }
            
            return;
        }
    }
    
    else if(strcmp(argv[0],"bg")==0)
    {
        //validate input
        if(!checkBG(argv))
            return;
               

        if (argv[1][0]=='%')
        {
            argv[1][0]='0';
            int jid=atoi(argv[1]);

            job=getjobjid(jobs,jid);
            if(jid<1||job==NULL)
            {
                printf("%%%d: No such job\n",jid);
                return;
            }
            
            if(job->state==ST)
            {
                kill(-job->pid,SIGCONT);
                job->state=BG;
                printf("[%d] (%d) ",job->jid,job->pid);
        	    printf("%s",job->cmdline);               
            }
            
            return;
        }
        
        else
        {
            int pid=atoi(argv[1]);

            job=getjobpid(jobs,pid);
            
            if(pid<1||job==NULL)
            {
                printf("(%d): No such process\n",pid);
                return;
            }
                   
            if(job->state==ST)
            {
                kill(-job->pid,SIGCONT);
                job->state=BG;
                printf("[%d] (%d) ",job->jid,job->pid);
        	    printf("%s",job->cmdline);               
            }
            
            return;
        }
        
        return;
    }
    
    return;
}


/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
    struct job_t* job;
    
    while((job=getjobpid(jobs,pid))!=NULL&&(job->state==FG))
        sleep(1);	//one second sleep
    
    return;
}


/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//

/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
void sigchld_handler(int sig)
{
    int pid=0;
    int stat=-1;
    
    do
    {
		pid=waitpid(-1,&stat,WNOHANG|WUNTRACED);   
		
        if(pid>0)
        {
            if(WIFEXITED(stat))
				deletejob(jobs,pid);
				
			else
			{
				if(WIFSIGNALED(stat))
				{
					if(WTERMSIG(stat)==2)
						printf("Job [%d] (%d) terminated by signal %d\n",pid2jid(pid),pid,WTERMSIG(stat));
					deletejob(jobs,pid);
				}
				
				else if(WIFSTOPPED(stat))
				{
					getjobpid(jobs, pid)->state=ST;
					printf("Job [%d] (%d) stopped by signal %d\n",pid2jid(pid),pid,WSTOPSIG(stat));
				}
			}
        }        
    }while(pid>0);
    
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.  
//
void sigint_handler(int sig)
{
    struct job_t* victim=NULL;
    
    for(int i=0;i<MAXJOBS;i++)
    {        
        if(getjobjid(jobs,i)!=NULL&&getjobjid(jobs,i)->state==FG)
            victim=getjobjid(jobs,i);     
    }
    
    if(victim!=NULL)
        kill(-(victim->pid),SIGINT);

    return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.  
//
void sigtstp_handler(int sig)
{
    struct job_t* victim=NULL;
    
    for(int i=0;i<MAXJOBS;i++)
    {        
        if(getjobjid(jobs,i)!=NULL&&getjobjid(jobs,i)->state==FG)
            victim=getjobjid(jobs,i);
    }

    if(victim!=NULL) 
        kill(-victim->pid,SIGTSTP);                

    return;
}

/*********************
 * End signal handlers
 *********************/
