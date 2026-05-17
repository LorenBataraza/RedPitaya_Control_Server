#include <sys/socket.h>
#include <netinet/in.h>

#include "api.cpp"
// Messaje Reader Thread - Only Prints to output responses and STDOUT
void messageReader(sockaddr_in serv_addr){
	char buf[1024];
	int pointer =0;
	// Cfg Message socket
	
	while(1){
		// Receive packet
		while(res=read(<message_receiber_socket>, ) > 0){}
            // Interpret read packet as cmd,

		if(cmd.type != RESPONSE){
			// Error 
		}else{
		
			// ADD previous details to output
			std::cout << cmd.Header.cmd_id
			
			// Think about text adding 
			switch(<>){
				case()
			}
			
			// Outputs expected buffer  
			std::cout << buf;
		}	
	}
}

// Completar
cmd_t tb_hello_cmd {

};

cmd_t tb_list_cmd {

};


void * writecmd_and_wait(socket, tb_hello_cmd){
	// write cmd 
	write()
	// wait for response
	read()
}

main(){
	admin_id admin_id= 1
	int command_id=0;
	
	// Reader thread 
	pthread(messageReader, <control_IP>)
	
	// Command Writer Thread
	writecmd_and_wait(socket, tb_hello_cmd)
	writecmd_and_wait(socket, tb_hello_cmd)
}