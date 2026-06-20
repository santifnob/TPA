#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- CONFIGURACIÓN DE RED Y BROKER ---
const char* ssid = "TU_WIFI_SSID";
const char* password = "TU_WIFI_PASSWORD";
const char* mqtt_server = " test.mosquitto.org"; // O la IP del Broker de la UTN
const char* mqtt_token = "TOKEN_DE_NUESTRO_GRUPO"; // Access Token de ThingsBoard

// --- TÓPICOS OFICIALES (APARTADO 9.1 DEL ENUNCIADO) ---
const char* topic_telemetry = "utn/2026/c02/g01/telemetry";
const char* topic_cmd = "utn/2026/c02/g01/cmd";

WiFiClient espClient;
PubSubClient client(espClient);
String bufferSerie = "";

void setup() {
  Serial.begin(115200);
  bufferSerie.reserve(64);
  
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback_mqtt); // Función para escuchar comandos de la nube
}

void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
}

// --- CALLBACK: RECEPCIÓN DE COMANDOS DESDE LA NUBE (RPC) ---
void callback_mqtt(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) { message += (char)payload[i]; }
  
  // Parseo del JSON entrante enviado por el Broker/Dashboard
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, message);
  if (!error) {
    // Si el formato es de ThingsBoard o el estándar del enunciado
    const char* method = doc["method"];
    double params = doc["params"];
    
    // Traducción inmediata al formato Serie string para el Arduino UNO
    if (String(method) == "setSetpoint") {
      Serial.print("SETPOINT:"); Serial.println(params);
    } else if (String(method) == "setKp") {
      Serial.print("KP:"); Serial.println(params);
    } else if (String(method) == "setKi") {
      Serial.print("KI:"); Serial.println(params);
    } else if (String(method) == "setAuto") {
      if(params == 1) Serial.println("AUTO:1");
      else Serial.print("MANUAL:"); Serial.println(doc["pwmManual"].as<double>());
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    // Se conecta usando el Access Token como usuario MQTT
    // if (client.connect("NodeMCU_Gateway", mqtt_token, NULL)) {
    if (client.connect("Gateway_Grupo01")) {
      client.subscribe("v1/devices/me/rpc/request/+"); // Suscripción nativa TB
      client.subscribe(topic_cmd);                    // Suscripción estándar UTN
    } else {
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) { reconnect(); }
  client.loop();

  // --- LECTURA DEL PUERTO SERIE (Datos provenientes del Arduino UNO) ---
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n') {
      procesarYPublicar(bufferSerie);
      bufferSerie = "";
    } else {
      bufferSerie += c;
    }
  }
}

// --- PARSEO CSV Y PUBLICACIÓN MQTT ---
void procesarYPublicar(String data) {
  data.trim();
  
  // Separación de las variables por comas (CSV)
  int primeraComa = data.indexOf(',');
  int segundaComa = data.indexOf(',', primeraComa + 1);
  int terceraComa = data.indexOf(',', segundaComa + 1);
  
  if (primeraComa != -1 && segundaComa != -1 && terceraComa != -1) {
    String y_ldr = data.substring(0, primeraComa);
    String r_set = data.substring(primeraComa + 1, segundaComa);
    String e_err = data.substring(segundaComa + 1, terceraComa);
    String u_pwm = data.substring(terceraComa + 1);

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
    client.publish(topic_telemetry, jsonBuffer);          // Para estándar UTN
  }
}
