#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PIPE_INIT(name) \
	int name[2];\
	pipe(name)\


void pipeline(int index,int pre_p[2]){
	if(index<9){
		int p[2];
		pipe(p);
		if(fork()==0){
			close(pre_p[0]);
			close(pre_p[1]);
			pipeline(index+1,p);
		}else{
			close(p[0]);
			close(pre_p[1]);
			int num=0;
			while(read(pre_p[0],&num,sizeof(int))){
				if(num!=1&&(num%(index+1)!=0||num==index+1)){
					//printf("prime %d\n",num);
					write(p[1],&num,sizeof(int));
				}
			}
			close(p[1]);
			close(pre_p[0]);
			while(wait((void*)0)>0);
			exit(0);
		}
	}
	else{
		close(pre_p[1]);
		int num=0;
		while(read(pre_p[0],&num,sizeof(int))==sizeof(int)){
			printf("prime %d\n",num);
		}
		close(pre_p[0]);
		exit(0);
	}
}



int 
main(){
	int p[2];
	pipe(p);
	if(fork()==0){
		pipeline(1,p);	
	
	}else{
		close(p[0]);
		for(int i=0;i<=35;i++){
			write(p[1],&i,sizeof(int));
		}
		close(p[1]);
	}
	while(wait((void*)0)>0);
	exit(0);
}


