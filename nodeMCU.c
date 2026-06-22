#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h> 

// --- CONFIGURACIÓN DE RED Y BROKER ---
const char* ssid = "Deptosanti";
const char* password = "46447073";
const char* mqtt_server = "mqtt.thingsboard.cloud"; 
const char* mqtt_token = "LSNeLOfTavr7SaYnvYkm";    

// --- TÓPICOS OFICIALES ---
const char* topic_telemetry = "utn/2026/c02/g01/telemetry";
const char* topic_cmd = "utn/2026/c02/g01/cmd";

// --- COMUNICACIÓN CON EL ARDUINO UNO ---
const int RX_UNO = 14; // D5 - Conectar al TX_UNO (Pin 9) mediante divisor
const int TX_UNO = 12; // D6 - Conectar al RX_UNO (Pin 8) de forma directa
SoftwareSerial serialUno(RX_UNO, TX_UNO);

// --- OBJETOS Y VARIABLES GLOBALES --- (Sin duplicados)
WiFiClient espClient;
PubSubClient client(espClient);
String bufferSerie = "";
String comandoPendiente = ""; // "Libreta" para la ventana de silencio

void setup() {
  Serial.begin(9600);       
  serialUno.begin(9600);    
  bufferSerie.reserve(64);

  Serial.println("Previo al WIFI");
  setup_wifi();
  Serial.println("WiFi listo");

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback_mqtt); 
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando a: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado!");
}

// --- CALLBACK: RECEPCIÓN DE COMANDOS DESDE LA NUBE (RPC) ---
void callback_mqtt(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) { message += (char)payload[i]; }

  Serial.print("MQTT IN: ");
  Serial.println(message);

  // 1. Extraemos el Request ID del topic para poder responder
  String topicStr = String(topic);
  int lastSlash = topicStr.lastIndexOf('/');
  String requestId = topicStr.substring(lastSlash + 1);

  // 2. Parseo del JSON 
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (!error) {
    const char* method = doc["method"];
    double params = doc["params"];

    // 3. Guardamos el comando en la "Libreta" en lugar de enviarlo a ciegas
    if (String(method) == "setSetpoint") {
      comandoPendiente += "SP:" + String(params) + "\n";
    } else if (String(method) == "setKp") {
      comandoPendiente += "KP:" + String(params) + "\n";
    } else if (String(method) == "setKi") {
      comandoPendiente += "KI:" + String(params) + "\n";
    } else if (String(method) == "setKd") {
      comandoPendiente += "KD:" + String(params) + "\n";
    } else if (String(method) == "setAuto") {
      if (params == 1) {
        comandoPendiente += "MODO:1\n";
      } else {
        comandoPendiente += "MODO:0\n";
      }
    } else if(String(method) == "setPwmManual"){
      comandoPendiente += "MAN:" + String(params) + "\n";
    }

    // 4. ACUSE DE RECIBO A THINGSBOARD (Evita el timeout)
    String responseTopic = "v1/devices/me/rpc/response/" + requestId;
    client.publish(responseTopic.c_str(), "{\"status\":\"ok\"}");
    
  } else {
    Serial.print("Error parseando JSON: ");
    Serial.println(error.c_str());
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando a MQTT...");
    if (client.connect("Gateway_Grupo01", mqtt_token, NULL)) {
      Serial.println("conectado");
      client.subscribe("v1/devices/me/rpc/request/+"); 
      client.subscribe(topic_cmd);                      
    } else {
      Serial.print("fallo, rc=");
      Serial.print(client.state());
      Serial.println(" reintentando en 5s");
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) { reconnect(); }
  client.loop();

  // --- LECTURA DE TELEMETRÍA Y VENTANA DE SILENCIO ---
  while (serialUno.available() > 0) {
    char c = serialUno.read();
    
    if (c == '\n' || c == '\r') {
      if (bufferSerie.length() > 0) {
        
        // 1. Procesamos los datos que llegaron
        procesarYPublicar(bufferSerie);
        bufferSerie = "";

        // 2. ¡Ventana Segura! Apenas el Arduino termina de hablar, inyectamos los comandos
        if (comandoPendiente != "") {
          serialUno.print(comandoPendiente);
          comandoPendiente = ""; // Vaciamos la libreta
        }
      }
    } else {
      bufferSerie += c;
      // Seguro contra mensajes pegados
      if (bufferSerie.length() > 40) {
        bufferSerie = "";
      }
    }
  }
}

// --- PARSEO CSV Y PUBLICACIÓN MQTT ---
void procesarYPublicar(String data) {
  data.trim();

  int primeraComa = data.indexOf(',');
  int segundaComa = data.indexOf(',', primeraComa + 1);
  int terceraComa = data.indexOf(',', segundaComa + 1);

  if (primeraComa != -1 && segundaComa != -1 && terceraComa != -1) {
    String y_ldr = data.substring(0, primeraComa);
    String r_set = data.substring(primeraComa + 1, segundaComa);
    String e_err = data.substring(segundaComa + 1, terceraComa);
    String u_pwm = data.substring(terceraComa + 1);

    // Filtro contra valores matemáticamente imposibles (picos falsos)
    if (y_ldr.toFloat() > 1024 || r_set.toFloat() > 1024) return;

    StaticJsonDocument<200> doc;
    doc["y"] = y_ldr.toFloat();
    doc["r"] = r_set.toFloat();
    doc["e"] = e_err.toFloat();
    doc["u"] = u_pwm.toInt();
    doc["mode"] = "AUTO"; 

    char jsonBuffer[200];
    serializeJson(doc, jsonBuffer);

    client.publish("v1/devices/me/telemetry", jsonBuffer); 
    client.publish(topic_telemetry, jsonBuffer);             
  }
}
