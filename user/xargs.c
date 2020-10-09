#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/param.h"

#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"



int
main(int argc,char*argv[]){
	int i,j;
	int num;
	char* exec_argv[MAXARG];
	for(num=1;num<argc;num++){
		exec_argv[num-1]=argv[num];		
	}
	num=num-1;//修正
	char buf[512];
	char arg_buf[512];
	char* p;
	int index=0;
	
	if(read(0,buf,2048)<=0){
		printf("xargs: read error\n");
	}
	for(i=0;i<strlen(buf);i++){
		if(buf[i]=='\n'){
			p=buf+index;
			memmove(arg_buf,p,i-index);
			arg_buf[i-index]=0;
			exec_argv[num]=arg_buf;
			index=i+1;
			if(fork()==0){
				exec(exec_argv[0],exec_argv);
			}
		}
	}
	//p=buf+index;
	//memmove(arg_buf,p,i-index);
	//arg_buf[i]=0;
	//exec_argv[num]=arg_buf;
	//if(fork()==0){
	//	exec(exec_argv[0],exec_argv);
	//}
	
	while(wait((void*)0)>0);
	exit(0);
}
