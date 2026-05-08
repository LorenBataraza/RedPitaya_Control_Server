#include <stdio.h>
#include<map>


enum api_family{
	common=0,       // rp_api/common
	oscilloscope,   // rp /
	registers
};

// Qué comparación tiene 
struct api{
	api_family mayor;
	int unsigned minor;
};

typedef std::map<const api, char *> names{

}

struct __attribute__((__packed__)) Header 
{
    api api_number;
    uint16_t size8;
};


// En el ejemplo define diferentes tipos de datos para lo body de todos los 
// paquetes de entrada, en este caso deberíamos hacer una


// Ejemplo
// Tengo la esctructa de datos osc_control_s con todos los valores internos de este