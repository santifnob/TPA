#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h> // Puerto serie virtual hacia el Arduino UNO

// --- CONFIGURACIÓN DE RED Y BROKER ---
const char* ssid = "Deptosanti";
const char* password = "46447073";
const char* mqtt_server = "mqtt.thingsboard.cloud"; // O la IP del Broker de la UTN
const char* mqtt_token = "LSNeLOfTavr7SaYnvYkm";    // Access Token de ThingsBoard

// --- TÓPICOS OFICIALES (APARTADO 9.1 DEL ENUNCIADO) ---
const char* topic_telemetry = "utn/2026/c02/g01/telemetry";
const char* topic_cmd = "utn/2026/c02/g01/cmd";

// --- COMUNICACIÓN CON EL ARDUINO UNO ---
// El puerto Serial (USB) queda libre para depuración por el monitor serie.
// Usamos SoftwareSerial en D5/D6 para hablar con el UNO.
// D5 = GPIO14, D6 = GPIO12 (pines seguros, sin restricciones de boot).
const int RX_UNO = 14; // D5 - NodeMCU RECIBE del UNO (conectar al TX_NODE del UNO, pin 9)
const int TX_UNO = 12; // D6 - NodeMCU TRANSMITE al UNO (conectar al RX_NODE del UNO, pin 8)
SoftwareSerial serialUno(RX_UNO, TX_UNO);

WiFiClient espClient;
PubSubClient client(espClient);
String bufferSerie = "";

void setup() {
  Serial.begin(9600);       // USB, solo para debug por monitor serie
  serialUno.begin(9600);    // Canal de datos real hacia el UNO
  bufferSerie.reserve(64);

  Serial.println("Previo al WIFI");
  setup_wifi();
  Serial.println("WiFi listo");

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback_mqtt); // Función para escuchar comandos de la nube
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

  Serial.println();
  Serial.println("WiFi conectado!");
  Serial.print("Direccion IP: ");
  Serial.println(WiFi.localIP());
}

// --- CALLBACK: RECEPCIÓN DE COMANDOS DESDE LA NUBE (RPC) ---
void callback_mqtt(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) { message += (char)payload[i]; }

  Serial.print("MQTT IN: ");
  Serial.println(message);


  // Parseo del JSON entrante enviado por el Broker/Dashboard
  StaticJsonDocument<200> doc;
  
  DeserializationError error = deserializeJson(doc, message);
  if (!error) {
    
    const char* method = doc["method"];
    double params = doc["params"];
    delay(50);

    Serial.println(method);
    Serial.println(params);
    // Traducción inmediata al formato que entiende procesarComando() en el UNO:
    // claves válidas -> SP, KP, KI, KD, MODO, MAN
    if (String(method) == "setSetpoint") {
      serialUno.print("SP:"); serialUno.println(params);
    } else if (String(method) == "setKp") {
      serialUno.print("KP:"); serialUno.println(params);
    } else if (String(method) == "setKi") {
      serialUno.print("KI:"); serialUno.println(params);
    } else if (String(method) == "setKd") {
      serialUno.print("KD:"); serialUno.println(params);
    } else if (String(method) == "setAuto") {
      if (params == 1) {
        serialUno.println("MODO:1");
      } else {
        serialUno.println("MODO:0");
        serialUno.print("MAN:"); serialUno.println(doc["pwmManual"].as<double>());
      }
    }
  } else {
    Serial.print("Error parseando JSON: ");
    Serial.println(error.c_str());
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando a MQTT...");
    // Conexión autenticada con el Access Token de ThingsBoard como usuario MQTT
    if (client.connect("Gateway_Grupo01", mqtt_token, NULL)) {
      Serial.println("conectado");
      client.subscribe("v1/devices/me/rpc/request/+"); // Suscripción nativa TB
      client.subscribe(topic_cmd);                      // Suscripción estándar UTN
    } else {
      Serial.print("falló, rc=");
      Serial.print(client.state());
      Serial.println(" reintentando en 5s");
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) { reconnect(); }
  client.loop();

  // --- LECTURA DEL PUERTO SERIE VIRTUAL (Datos provenientes del Arduino UNO) ---
  while (serialUno.available() > 0) {
    char c = serialUno.read();
    if (c == '\n' || c == '\r') {
      if (bufferSerie.length() > 0) {
        procesarYPublicar(bufferSerie);
        bufferSerie = "";
      }
    } else {
      bufferSerie += c;
      if (bufferSerie.length() > 40) { 
        bufferSerie = ""; 
      }
    }
  }
}

// --- PARSEO CSV Y PUBLICACIÓN MQTT ---
void procesarYPublicar(String data) {
  data.trim();

  // Separación de las variables por comas (CSV): y,r,e,u
  int primeraComa = data.indexOf(',');
  int segundaComa = data.indexOf(',', primeraComa + 1);
  int terceraComa = data.indexOf(',', segundaComa + 1);

  if (primeraComa != -1 && segundaComa != -1 && terceraComa != -1) {
    String y_ldr = data.substring(0, primeraComa);
    String r_set = data.substring(primeraComa + 1, segundaComa);
    String e_err = data.substring(segundaComa + 1, terceraComa);
    String u_pwm = data.substring(terceraComa + 1);

    if (y_ldr.toFloat() > 1024 || r_set.toFloat() > 1024) return;

    // Construcción del Payload JSON estricto bajo formato del enunciado 9.1
    StaticJsonDocument<200> doc;
    doc["y"] = y_ldr.toFloat();
    doc["r"] = r_set.toFloat();
    doc["e"] = e_err.toFloat();
    doc["u"] = u_pwm.toInt();
    doc["mode"] = "AUTO"; // Estado dinámico del lazo

    char jsonBuffer[200];
    serializeJson(doc, jsonBuffer);

    // Publicación en los brokers correspondientes
    client.publish("v1/devices/me/telemetry", jsonBuffer); // Para ThingsBoard
    client.publish(topic_telemetry, jsonBuffer);             // Para estándar UTN
  }
}
