#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *path,char *file){
	char buf[512],*p;
	int fd; 
	struct stat st;
	struct dirent de;
	if((fd=open(path,0))<0){
		fprintf(2,"find: cannot open %s\n",path);
		return;
	}
	if(fstat(fd,&st)<0){
		fprintf(2,"find: cannot stat %s\n",path);
		close(fd);
		return;
	}
	switch(st.type){
		case T_FILE:
			//printf("find: %s is not a dir\n",path);
			close(fd);
			return;
		case T_DIR:
			//length check;
			strcpy(buf,path);
			p=buf+strlen(buf);
			*p++='/';

			while(read(fd,&de,sizeof(de))==sizeof(de)){
				if(de.inum==0||memcmp(de.name,".",1)==0||memcmp(de.name,"..",2)==0)
					continue;
				
				memmove(p,de.name,DIRSIZ);
				p[DIRSIZ]=0;
				if(memcmp(de.name,file,strlen(file))==0){
					printf("%s\n",buf);
				}
				else{
					find(buf,file);
				}	
				
			}
			break;
	}
	close(fd);
}

int
main(int argc,char *argv[]){

	if(argc!=3){
		printf("Find Error\n");
	}
	else{
		find(argv[1],argv[2]);
	}
	exit(0);
}

