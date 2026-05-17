#include "inttypes.h"

typedef uint32_t request_id; 
typedef uint32_t admin_id;
typedef uint64_t device_mac;  // MAC es de 48 bits pero es suficiente.

//  Hardware enums, etc
#include "RedPitaya/rp-api/api-hw-profiles/include/rp_hw-profiles.h"

// Necesito una estructura en la que guarde la info de cada cliente
struct hardware_clients{
	device_mac hardware_id; // MAC?
	int hardware_id;    // Determinado por Control
	rp_HPeModels_t model;
	rp_HPeZynqModels_t ZynqModel;
	// Other data 
};



enum api_family : uint16_t {
    API_CONTROL     = 0,    // mensajes del propio protocolo
    API_SYSTEM      = 1,    // rp_Init, rp_Release, rp_GetVersion
    API_ACQUISITION = 2,    // rp_Acq*
    API_GENERATION  = 3,    // rp_Gen*
    API_DIGITAL_IO  = 4,    // rp_Dpin*
    API_ANALOG_IO   = 5,    // rp_Apin*
};

// Minor para los mensajes de control 
enum control_fn : uint16_t {
	// Conexión de HW
	// CTRL_DISCOVERY= 0      ,  // Control -> Hardware por Broadcast 
    CTRL_HELLO             ,     // Request: Hardware -> Control: "soy <device_mac/ip>"
					             // Response: Control -> Hardware, aceptado
    
    // Listar dispositivos
    CTRL_LIST_DEVICES      ,    // Request: Admin -> Control
								// Response: Control -> Admin
    
    // CTRL_CMD             ,  // Admin -> Control: ejecutá esto en device X
    CTRL_DEVICE_GONE       ,  // Tipo assert: Control -> Admin: notificación de desconexión
    
    // Out Program
    STD_OUTDEVICE         , // Hardware -> Control y Control -> Admin
	NUM_CONTROL_CMD        ,
};

enum flag: uint8_t{
	REQUEST = 0,
	RESPONSE,
	EVENT // Mensajes que no están pensado para tener respuesta, como CTRL_DEVICE_GONE. 
};

struct __attribute__((__packed__)) body_t{
	device_mac destination_device; // MAC
	uint32_t request_id;           // Lo determina control y admin, 

	union params{
		int int_1;
		double double_1;
		struct INT_INT{
			int int1;
			int int2;
		};
	};
};


struct __attribute__((__packed__)) api_id{
	api_family api_family; // Mayor
	uint16_t function_id;  // Minor
};

struct __attribute__((__packed__)) header_t {
    uint16_t version      ; // Versión de lib (0x0)
	api_id api_id         ; // Mayor and minor
    uint32_t payload_size ; // Bytes del cuerpo que siguen
    flag type             ; 
};

struct __attribute__((__packed__)) cmd_t {
    header_t header;
    body_t body;
};



// Helper functions

// Read socket until end of comand, store in a buffer return in with cmd type
cmd_t * read_cmd();

//
int * write_cmd();
