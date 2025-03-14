#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <cstdlib>
#include <string>
#include <string.h>
#include <vector>
#include <sstream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sstream>
#include <cstring>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <sys/stat.h>

using namespace std;

class PipeFunction{
	public:
		int count;
		int temp;
		int fd[2];
};

class Cmd{
	public:
		string command;
		char spot;
		int toPine;
		int fd_in, fd_out;
		bool fdIn, fdOut;
		int pipeType; 
		vector<string> tokens;
		void setVar(string com, char m, int n); 
		int cType();
		void fg();
};


vector<PipeFunction> pipeList;
vector<Cmd> cmdList;

void Cmd::setVar(string com, char m, int n){
	command = com;
	spot = m;
	toPine = n;
}

void Cmd::fg(){
	stringstream ss(command);
	string temp;
	vector<string> temToken;

	while(ss >> temp)
		temToken.push_back(temp);

	tokens = temToken;
}

int Cmd::cType(){
	int t;
	string e = tokens.at(0); 

	if(e == "printenv")
		t = 1;
	else if(e == "setenv")
		t = 2;
	else if(e == "exit" || e == "EOF")
		t = 3;
	else
		t = 4;
	return t;
}

void sig_fork(int signo){
	int statusvalue;
	while(waitpid(-1,&statusvalue,WNOHANG) > 0){}
}

void initshell(){
	clearenv();
	setenv("PATH","bin:.", 1);
	signal(SIGCHLD,sig_fork);
}



void Input(string input){
	Cmd cmds; 
	int f = 0; 
	int set = 0; 
	int num = 0; 
	int spot = 0; 

	while(1){
		f = input.find_first_of("|!",set);
		if(f != set){
			num = 0;
			spot = input[f];
			if(spot == '|')
				cmds.pipeType = 1;
			else if(spot == '!')
				cmds.pipeType = 2;

			string temp = input.substr(set,f-set); 

			if(isdigit(input[f+1])){
				f++;
				int first = f;
				while(isdigit(input[f+1])){	
					f++;
				}
				num = atoi(input.substr(first,f-first+1).c_str());
			}

			cmds.fdIn = 0; 
			cmds.fdOut = 0;
			cmds.fd_in = 0;
			cmds.fd_out = 0;

			if(temp.size() != 0){
				temp.erase(0, temp.find_first_not_of(" "));
				temp.erase(temp.find_last_not_of(" ")+1);

				cmds.setVar(temp, spot, num); 
				cmds.fg();
				cmdList.push_back(cmds);
			}
		}
		if(f == string::npos)
			break;
		set = f+1;
	}		
}

void Pipee(int i){
	PipeFunction nPipe;
	nPipe.count = 0;
	
	if(cmdList.at(i).toPine > 0 && i == cmdList.size()-1)
		nPipe.count = cmdList.at(i).toPine;

	if(cmdList.at(i).spot == '|')
		cmdList.at(i).pipeType = 1;
	else if(cmdList.at(i).spot == '!')
		cmdList.at(i).pipeType = 2;
	else
		cmdList.at(i).pipeType = 0;
	if(nPipe.count > 0){
		bool f = 0;
		for(int j=0; j<pipeList.size(); j++){
			if(nPipe.count == pipeList.at(j).count){
				cmdList.at(i).fd_out = pipeList.at(j).fd[1];
				f = 1;
				break;
			}
		}
		if(f == 0){ 
			pipe(nPipe.fd);
			cmdList.at(i).fd_out = nPipe.fd[1]; 
			pipeList.push_back(nPipe);		
		}
	}
	else{ 
		if(cmdList.at(i).pipeType != 0){
			nPipe.count = -1;
			pipe(nPipe.fd);
			cmdList.at(i).fd_out = nPipe.fd[1]; 
			pipeList.push_back(nPipe);
		}
	}
	if(cmdList.at(i).pipeType != 0)
		cmdList.at(i).fdOut = 1;
}

void changeType(char *com[], int sizee, int i){
	vector<string> tok = cmdList.at(i).tokens;

	for(int j=0; j<sizee; j++){
		com[j] = (char*)tok.at(j).c_str();
	}
	com[sizee] = NULL;
	
}

int findIndex(vector<string> &v, string str){
	int i;
	for(i=0;i<v.size();i++)
	if(v[i] == str)
		break;
	if(i == v.size())
		i = -1;
	return i;
}

void Execommand(int i){
		int type = cmdList.at(i).cType();
		char* com[1000];
		if(type == 1){ //printenv
			char* envName = getenv(cmdList.at(i).tokens.at(1).c_str());
			if(envName != NULL)
				cout<< envName <<endl;
		}
		else if(type == 2){ //setenv
			setenv(cmdList.at(i).tokens.at(1).c_str(),
			       cmdList.at(i).tokens.at(2).c_str(), 1);
		}
		else if(type == 3){ 
			exit(0);
		}
		else if(type == 4){
			int statusvalue;
			FORK:
			pid_t child_pid = fork();			

			if(child_pid < 0){
				while(waitpid(-1,&statusvalue,WNOHANG) > 0){}
				goto FORK;

			}

			if(child_pid == 0){
				if(cmdList.at(i).fdIn == 1)
					dup2(cmdList.at(i).fd_in, STDIN_FILENO);
				if(cmdList.at(i).fdOut == 1){
					dup2(cmdList.at(i).fd_out, STDOUT_FILENO); 
					if(cmdList.at(i).pipeType == 2) 
						dup2(cmdList.at(i).fd_out, STDERR_FILENO);
				}

				for(int j=0; j<pipeList.size(); j++){
					close(pipeList.at(j).fd[0]);
					close(pipeList.at(j).fd[1]);
				}
				int index = findIndex(cmdList.at(i).tokens,">");
				if(index == -1) //not find
					changeType(com, cmdList.at(i).tokens.size(), i);
				else{
					string file = cmdList.at(i).tokens.back().data();
					//cout<<"file: "<<file<<endl;
					int fd = open(file.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC ,0600);
					dup2(fd, STDOUT_FILENO);
					close(fd);
					changeType(com, index, i);
				}

				if(execvp(com[0], com) == -1)
					cerr<<"Unknown command: ["<<com[0]<<"]."<<endl;
				exit(1);
			}
			else{
				if(cmdList.at(i).fdIn == 1){
					for(int j=0; j<pipeList.size(); j++){
						if(pipeList.at(j).fd[0] == cmdList.at(i).fd_in){
							//parentClose
							close(pipeList.at(j).fd[0]);
							close(pipeList.at(j).fd[1]);
							pipeList.erase(pipeList.begin()+j);
							break;
						}
					}
				}
				if(cmdList.at(i).pipeType == 0){
					//cout<<"1: "<<child_pid<<endl;
					waitpid(child_pid,&statusvalue, 0);
				}
				else{
					//cout<<"2: "<<child_pid<<endl;
					waitpid(-1,&statusvalue,WNOHANG);
					//waitpid(-1, NULL ,WNOHANG);
				}
			}
						
		}
}

int main() {	
	initshell();
	while(1){
		string input;
		cout<<"% ";
		getline(cin,input);

		if(input.empty() == true || input.find_first_not_of(" ",0) == string::npos)
			continue;

		Input(input);
		
		for(int i=0; i<cmdList.size(); i++){
			Pipee(i); //pipe
			if(i == 0){
				for(int j=0; j<pipeList.size(); j++){
					if(pipeList.at(j).count == 0){
						cmdList.at(i).fd_in = pipeList.at(j).fd[0];
						cmdList.at(i).fdIn = 1;
						break;
					}
				}
			}
			else{
				for(int j=0; j<pipeList.size(); j++){
					if(pipeList.at(j).count == -1){ 
						cmdList.at(i).fd_in = pipeList.at(j).fd[0];
						break;
					}
				}
				cmdList.at(i).fdIn = 1;
			}
			Execommand(i); 
		}
		for(int i=0; i<pipeList.size(); i++)
			pipeList.at(i).count--;
		cmdList.clear();
	}	
}

