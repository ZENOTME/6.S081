#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int 
main(int argc,char *argv[])
{
	int p_one[2];
	int p_two[2];
	pipe(p_one);//father -> child
	pipe(p_two);//child  -> father

	if(fork()!=0){
		char buf;
		close(0);
		dup(p_one[0]);
		close(p_one[0]);
		close(p_one[1]);

		close(p_two[0]);
		if(read(0,&buf,1)==1){
			printf("%d: received ping\n",getpid());
			write(p_two[1],"a",1);
		}
		//while(wait((void*)0)>0);
	}
	else{
		char buf;
		close(0);
		dup(p_two[0]);
		close(p_two[0]);
		close(p_two[1]);

		close(p_one[0]);
		write(p_one[1],"a",1);
		if(read(0,&buf,1)==1){
			printf("%d: received pong\n",getpid());
		}
	}
	exit(0);
}
