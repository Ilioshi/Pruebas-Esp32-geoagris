#include "trax_utils.h"
#include "DiagnosticProtocol.h"

/*********************************************************************************************
*                                           Defines                                          *
**********************************************************************************************/

#define GPS_UPDATE_INTERVAL_MS      1500

#define TRAX_UTILS_DEBUG 0

/*********************************************************************************************
*                                       Global Variables                                     *
**********************************************************************************************/

static bool G_force_US07 = false;

// Variable estática (solo va a estar dipsonible para las funciones de este archivo trax_utils.cpp) para almacenar el handle del semáforo, inicialmente NULL.
static SemaphoreHandle_t G_semaphoreUART = NULL;

// Variable para saber si ya configuró o no el Serial que se va a usar para comunicarse con el trax 
static bool G_serial_configured = false;

//Variables del GPS
static unsigned long G_gps_last = millis() - 5000;
static double G_gps_speed_kmh = -1.0; // Inicializado en -1
static double G_gps_heading = -1.0;   // Inicializado en -1
static std::string G_gps_time_string = "010170000000"; 

// Variable global para el estado de TR1
static bool G_tr1 = false;

/****************************************************************************************
*                                   Local Prototypes                                    *
*****************************************************************************************/

static void printForDebugTrax(const std::string& message);
static void serial_configure(void);
static std::string traxSendReceiveBase(const std::string& data);
static void update_gps_data(void);
static SemaphoreHandle_t getSemaphoreUART(void);


/****************************************************************************************
*                                   Public Functions                                    *
*****************************************************************************************/

// Function to send and receive data to Trax with mutex handling. Returns "" if no response
void traxInitialize() {
    serial_configure();
    (void)getSemaphoreUART();
}

std::string traxSendReceive(const std::string& data)
{
    std::string receivedData = "";

    // Send the data
    receivedData = traxSendReceiveBase(data);

    // Check if the response contains the '?' character
    if (receivedData.find('?') != std::string::npos)
    {
        // Password was missing, so now we send it
        receivedData = traxSendReceiveBase(">SPWgeo18ris<");
        if (receivedData.find(">RPW") < receivedData.find("<"))
        {
            // Password received correctly, now we send the data
            receivedData = traxSendReceiveBase(data);
        }
        else
        {
            receivedData = "";
        }
    }
    return receivedData; // Return the final response
}

double traxGpsSpeedKmh(void) {
    update_gps_data();
    return G_gps_speed_kmh;
}

double traxGpsHeading(void) {
    update_gps_data();
    return G_gps_heading;
}

std::string traxGpsTimeString(void) {
    update_gps_data();
    return G_gps_time_string;
}

std::string traxGetMachineName(void) {
    std::string machineName = "unknown";
    std::string response = traxSendReceive(">QUS0B<");
    
    // Expected format: >RUS0B,4153,gsz01,1.00,34,Geoagris,137,Pulverizadora,6,29018660;ID=0857543;*02<
    if (response.find(">RUS0B") != std::string::npos)
    {
        size_t firstComma = response.find(',');
        if (firstComma != std::string::npos)
        {
            size_t secondComma = response.find(',', firstComma + 1);
            if (secondComma != std::string::npos)
            {
                size_t thirdComma = response.find(',', secondComma + 1);
                if (thirdComma != std::string::npos)
                {
                    machineName = response.substr(secondComma + 1, thirdComma - secondComma - 1);
                }
            }
        }
    }
    
    return machineName;
}

/**
 * @brief Gets the number of WiFi credentials from TRAX and stores them in the provided array.
 *
 * @param credentials Output array of `WifiCredentials*` where credentials are stored.
 *        Must have space for `MAX_WIFI_CREDENTIALS` elements.
 * @returns `int` Number of credentials obtained (0 on error).
 */
int traxGetWifiCredentialsCount(WifiCredentials* credentials) {
    if (credentials == nullptr) {
        printForDebugTrax("Error: null pointer");
        return 0;
    }
    
    // Request credentials from TRAX
    std::string response = traxSendReceive(">QUS0F<");
    
    printForDebugTrax("Content: '" + response + "'");

    if (response.empty() || response.length() < 10) {
        printForDebugTrax("Empty or invalid response");
        return 0;
    }
    
    // Search for first ',' after the command
    size_t firstComma = response.find(',');
    if (firstComma == std::string::npos) {
        printForDebugTrax("Invalid format");
        return 0;
    }
    
    // Search for ';' that marks the end of the data (before ID=)
    size_t endMarker = response.find(';');
    if (endMarker == std::string::npos) {
        // If no ';', search for '<'
        endMarker = response.find('<');
        if (endMarker == std::string::npos) {
            endMarker = response.length();
        }
    }
    
    // Extract data between ',' and ';'
    std::string dataPart = response.substr(firstComma + 1, endMarker - firstComma - 1);
    
    printForDebugTrax("Parsing: " + dataPart);
    
    // Parse SSID:PASSWORD pairs
    int count = 0;
    size_t pos = 0;
    
    while (count < MAX_WIFI_CREDENTIALS && pos < dataPart.length()) {
        // Search for first ':' (end of SSID)
        size_t firstColon = dataPart.find(':', pos);
        if (firstColon == std::string::npos) {
            break; // No more pairs
        }
        
        // Extract SSID
        std::string ssid = dataPart.substr(pos, firstColon - pos);
        
        // Search for second ':' (end of PASSWORD)
        size_t secondColon = dataPart.find(':', firstColon + 1);
        std::string password;
        
        if (secondColon == std::string::npos) {
            // Last pair - PASSWORD goes until the end
            password = dataPart.substr(firstColon + 1);
        } else {
            // PASSWORD between ':'
            password = dataPart.substr(firstColon + 1, secondColon - firstColon - 1);
        }
        
        // Store in the provided array
        credentials[count].ssid = ssid;
        credentials[count].password = password;
        
        printForDebugTrax("SSID='" + ssid + "', PASSWORD='" + password + "'");
        
        count++;
        
        if (secondColon == std::string::npos) {
            break; // Last pair
        }
        
        pos = secondColon + 1;
    }
    
    printForDebugTrax("Total credentials: " + std::to_string(count));
    
    return count;
}


/**
 * @brief Configures TRAX to use BLE for outgoing connections. 
 */
void traxSetIP0() {
    if(G_tr1)
    {
        traxSendReceive(">SLD IP0<");
        traxSendReceive(">SSD IP0<");
        G_tr1 = false;
    }
}

/**
 * @brief Configures TRAX to send messages via UDP.
 */
void traxSetTR1() {
    if(!G_tr1)
    {
        traxSendReceive(">SLD TR1<");
        traxSendReceive(">SSD TR1<");
        G_tr1 = true;
    }
}


/****************************************************************************************
*                                   Local Functions                                     *
*****************************************************************************************/

static void printForDebugTrax(const std::string& message) {
    #if TRAX_UTILS_DEBUG
        Serial.printf("[TraxUtils] %s\n", message.c_str());
    #endif
}

// Configura el serial. Si ya lo configuró, no lo vuelve a configurar 
static void serial_configure(void) {
    if (!G_serial_configured) {
        Serial.begin(115200);       // TODO: Matar estas funciones, incorporar AgxSerial
        G_serial_configured = true;
    }
}

static std::string traxSendReceiveBase(const std::string& data) {
    serial_configure();
    SemaphoreHandle_t sem = getSemaphoreUART();
    std::string receivedData;
    receivedData.reserve(256);  // Pre-asignar memoria para evitar reallocaciones

    // Toma el semáforo
    if (xSemaphoreTake(sem, portMAX_DELAY) == pdTRUE)
    {
        // Limpiar el buffer de entrada del Serial
        while (Serial.available() > 0)
        {
            Serial.read(); // Leer y descartar cualquier dato disponible
        }

        Serial.println(data.c_str()); // Enviar datos

        // Busca respuesta
        unsigned long startTime = millis();
        bool start = false;
        while ((millis() - startTime) < 1000)
        {
            if (Serial.available())
            {
                char c = Serial.read();
                if (c == '>')
                {
                    receivedData.clear();
                    start = true;
                }
                if (start)
                {
                    receivedData.push_back(c);
                    if (receivedData.size() > 768) {
                        receivedData.clear();
                        start = false;
                    }
                }

                // Verificar si se ha recibido el carácter '<'
                if (c == '<' && start)
                {
                    if (DiagnosticProtocol::responseMatches(data, receivedData)) {
                        xSemaphoreGive(sem);
                        return receivedData;
                    }
                    // Ignore asynchronous reports or late replies to another command.
                    receivedData.clear();
                    start = false;
                }
            }
            else
            {
                // Esperar para evitar el bloqueo del sistema
                delay(10);
            }
        }
        // Liberar el semáforo
        xSemaphoreGive(sem);
    }
    return ""; // Never report a partial frame as a successful transaction.
}

// Función interna que retorna el semáforo, creándolo si es necesario.
static SemaphoreHandle_t getSemaphoreUART(void) {
    if (G_semaphoreUART == NULL) {
        G_semaphoreUART = xSemaphoreCreateBinary();
        xSemaphoreGive(G_semaphoreUART);
    }
    return G_semaphoreUART;
}

// Función que consulta y actualiza los datos del GPS
static void update_gps_data() {

    if (millis() - G_gps_last < GPS_UPDATE_INTERVAL_MS) {
            return;     //solo consulto al trax si pasaron más de 1 segundo y medio
    }
    G_gps_last = millis();

    //Blanqueo
    G_gps_speed_kmh = -1;
    G_gps_heading = -1;
    G_gps_time_string = "010170000000";
    
    // Enviar consulta del reporte 07 para el parseo
    std::string response = traxSendReceive(">QUS07<");
    if (G_force_US07)
    {
        response = ">RUS07,09092420061000003419.5533S06012.0261W311E70000000000-125000.0-0400000000001,76.6,12.1,99;ID=2577552;#LOG:04A0;*02<";
    }
    if (!(response.find(">RUS07") < response.find("<")))
    {
        return;
    }

    // Fecha y hora
    bool success_time = true;
    G_gps_time_string = response.substr(7, 12);
    success_time = (G_gps_time_string != "010170000000" && esNumerico(G_gps_time_string));
    if (!success_time) {
        return; 
    }

    // Extraer la velocidad en nudos
    size_t firstComma = response.find(',');
    size_t secondComma = response.find(',', firstComma + 1);
    size_t thirdComma = response.find(',', secondComma + 1);
    size_t fourthComma = response.find(',', thirdComma + 1);

    if (thirdComma != std::string::npos && fourthComma != std::string::npos)
    {
        std::string speedStr = response.substr(thirdComma + 1, fourthComma - thirdComma - 1);
        // Verificar que el string de velocidad sea numérico
        if (esNumerico(speedStr))
        {
            G_gps_speed_kmh = std::stod(speedStr) * 1.852; // Convertir de nudos a km/h
        }
    }
    // Extraer el heading
    if (response.length() >= 47)
    {
        std::string headingStr = response.substr(44, 3);
        // Verificar que el string de heading sea numérico
        if (esNumerico(headingStr))
        {
            G_gps_heading = std::stod(headingStr);
        }
    }
    printForDebugTrax("Parseo de US07");
    printForDebugTrax(G_gps_time_string);
    printForDebugTrax(std::to_string(G_gps_speed_kmh));
    printForDebugTrax(std::to_string(G_gps_heading));
}
