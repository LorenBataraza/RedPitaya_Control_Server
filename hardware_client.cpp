///////////////////////////
/*


*/
///////////////////////////

#include "signal.h"
#include "api.cpp"
#include <stdio.h>


#define HARDWARE_IP localhost
#define RECEIVER_PORT 101010

// Todo complete example Pkg
cmd_t hardware_client_hello = {
	//
	{} 
	// Datos de la Pitaya 
	{} 
};


cmd_t client_bye{

};

void catchSigEnv(){

    write_cmd(client_bye, socket)

}




int main(){

    // Redirecciono std a buffer interno
    char buffer[1024];
    setvbuf(stdout, buffer, _IOFBF, sizeof(buffer));

    while(1){

        // Connect to Control
        connect() 

        // Recibir paquete 
        while( (res = read()) >0){
            strcopy(cmd, res)
        };
        
        // Decodificar y llamar a función 
        res = switch(api_id) // :api_id
            case(funct_0): funct_0(cmd.params.INT.int_1) // Esto para todas las funciones
            case(funct_1): funct_n(cmd.params.INT.int_1),
            ...
            case(funct_n): funct_n(<params>)
        
        
        // Creo mensaje de Respuesta 
        cmd_t response_cmd={
            {
            VERSION
            
            RESPONSE
            }
        };
        
        // Write Response
        write_cmd(<Receiver message socket>, response_cmd);
    }

}