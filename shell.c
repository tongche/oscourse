#define _POSIX_SOURCE
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/param.h>
#include <stdbool.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int error;
static bool isExiting;
static bool isWaiting;
static pid_t pidWait;
enum commandType{
CD,
STATUS,
EXIT,
EXTERNAL
};

void INThandler(int signum)
{
   printf("Interrupt Signal:%d\n",SIGINT);
   if(isWaiting)
      kill(pidWait,SIGINT);
   signal(SIGINT,SIG_IGN);
}

void warning(char* err)
{
  printf("%s\n",err);
  return;
}

struct command{
int pipeToClose[2]; // folked process, self 
int pipeToCloseParent;
int inout[2];
enum commandType commandType;
char **argv; //head of the command parsed
int argc;
bool ground; // true fg, false background 
bool runnable;
};

typedef struct command command;

int
builtin_cd(int argc, char **argv)
{
   if(argc!=2) {
    warning("Wrong arguments");
    return -1;
}
  error=chdir(argv[1]);
  return (0);
}


int
builtin_status(int argc, char **argv)
{
        printf("%d\n",error);
	return (0);
}


static int
run_command(command* com)
{
 pid_t pid;
     if(!com->runnable)
         return 0;
 switch(com->commandType){
   case CD: return builtin_cd(com->argc,com->argv);
   case EXIT: 
        isExiting = true;
        return 0;  
   case STATUS: return builtin_status(com->argc,com->argv);
   case EXTERNAL:
        pid = fork();
        if (pid == 0) {
           if(com->pipeToClose[0]>=0)
                  close(com->pipeToClose[0]);
           if(com->inout[0] != STDIN_FILENO){
               dup2(com->inout[0],STDIN_FILENO);
               close(com->inout[0]);
            }
           if(com->inout[1] != STDOUT_FILENO){
               dup2(com->inout[1],STDOUT_FILENO);
               close(com->inout[1]);
            }               
         execvp(com->argv[0],com->argv);
        }
       else {
        if(com->pipeToClose[1]>=0)
              close(com->pipeToClose[1]);
        if(com->pipeToCloseParent >= 0)
              close(com->pipeToCloseParent);              
        if (com->ground == true) {
           isWaiting = true;
           pidWait = pid;
           waitpid(pid,&error,0);
         }
        isWaiting = false;
        return 0;
   }
   default: return 0;
  }
}

bool isspace_c(char c){      //custom implementation of isspace.
switch (c){
case ' ': return true;
case '\t': return true;
default: return false;
  }
}

static char *
parseword(char **pp)
{
	char *p = *pp;
	char *word;
	for (; isspace_c(*p); p++);
	word = p;
	for (; strchr(" \t;&|><\n", *p) == NULL; p++);
	*pp = p;
	return (p != word ? word : NULL);
}



void initializeCom(command* com, char **args)
{
        com->inout[0] = STDIN_FILENO;
        com->argc = 0;
        com->ground = true;
        com->commandType = EXTERNAL;
	com->inout[1] = STDOUT_FILENO;
	com->argv = args;
        com->pipeToClose[0] = -1;
        com->pipeToClose[1] = -1;
        com->pipeToCloseParent = -1;
        com->runnable = true;
}

static int
process(char *line)
{       
        char temp[100];
        command* nextCom;
        int ch;
	char *p, *word;
	char *args[100];
	int pip[2];
	int fd;
	p = line;
        command* com = (command*) malloc(sizeof(command));

        initializeCom(com,args);
	for (; *p != 0;) {
		word = parseword(&p);
		ch = *p;
		*p = 0;
                p++;
		if (word != NULL) {
                       if(com->argc == 0){
                            if(!strcmp(word,"cd"))
                                com->commandType = CD;
                            else if(!strcmp(word,"status"))
                                com->commandType = STATUS;
                            else if(!strcmp(word,"exit"))
                                com->commandType = EXIT;
                            else com->commandType = EXTERNAL;
                     }
			com->argc++;
                        com->argv[com->argc-1] = word;
			com->argv[com->argc] = NULL;
		}
          switch(ch){
            case ' ': break;
            case '\t': break; 
            case '\n': 
                     if(com->argc > 0)
                     return run_command(com); 
                     else return 0;
            case '>':
                 word = parseword(&p);
                 if (word == NULL)
                     return -1;
                 ch = *p;
                 *p = 0;
                 strcpy(temp,word);
                 *p = ch;
                 fd = open(temp,O_CREAT|O_WRONLY,0666);
                 if(fd < 0){
                    warning("File not found, try a new path");
                    return -1;
               }
                 com->inout[1] = fd;
                 break;
            case '<': 
                 word = parseword(&p);
                 if(word == NULL)
                     return -1;
                 ch = *p;
                 *p = 0;
                 strcpy(temp,word);
                 *p = ch;
                 fd = open(temp,O_CREAT|O_RDONLY,0666);
                 if(fd < 0){
                    warning("File not found, try a new path");
                    return -1;
               }                 
                 com->inout[0] = fd;
                 break;
           case '|':
              if (*p != '|'){
                 nextCom = (command*) malloc(sizeof(command));
                 initializeCom(nextCom,args);
                 if(com->runnable) {
                 pipe(pip);
                 com->inout[1] = pip[1];  //pipe, 0 for read, 1 for write
                 com->pipeToClose[0] = pip[0]; // close by child
                 com->pipeToClose[1] = pip[1];
                 nextCom->inout[0] = pip[0]; //read
                 nextCom->pipeToCloseParent = pip[0];
                 com->ground = false;
                 }
                 run_command(com);
                 if(!com->runnable)
                    nextCom->runnable = false;
                 free(com);
                 com = nextCom;
               }
              else {
                 p++;
                 run_command(com);
                  //run second command
                 nextCom = (command*) malloc(sizeof(command));
                 initializeCom(nextCom,args);
                 if(!error)
                    nextCom->runnable = false;
                 free(com);
                 com = nextCom;
              }
              break;      
           case '&':
               if (*p != '&') {
                 com->ground = false;
                 run_command(com);                   
                 nextCom = (command*) malloc(sizeof(command));
                 initializeCom(nextCom,args);
                 if(!com->runnable)
                    nextCom->runnable = false;
                 free(com);
                 com = nextCom;               
                 }
               else {
                 p++;
                 run_command(com);
                 free(com);
                 nextCom = (command*) malloc(sizeof(command));
                 initializeCom(nextCom,args);
                 com = nextCom;
                 if(error) 
                     com-> runnable = false;  
           }
          break;

          case ';': 
               if(com->argc > 0)
               run_command(com);
               nextCom = (command*) malloc(sizeof(command));
               initializeCom(nextCom,args);
               com = nextCom;
          break;              
          default: warning("Command syntax error.");
          return -1;   
          }
       }
  return 0;   // do next line
}

int
main(void)
{
        signal(SIGINT,SIG_IGN);
        char cwd[MAXPATHLEN+1];
	char line[1000];
	char *res;
        isWaiting = false; 
        isExiting = false;
	for (;;) {
          getcwd(cwd, sizeof(cwd));
	  printf("%s %% ", cwd);
	  res = fgets(line, sizeof(line), stdin);
          if (res == NULL)
		break;
          signal(SIGINT,INThandler);
          process(line);
           if(error)
           warning("The operation is Unsuccessful(exit status not 0)");
          signal(SIGINT,SIG_IGN);
          if(isExiting)
              return 0;
	}
	return (error);
}
