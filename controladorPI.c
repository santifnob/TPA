#include <SoftwareSerial.h>

// --- PINES FÍSICOS ---

const int pinLDR = A0;
const int pinLED = 3;
const int RX = 8;
const int TX = 9;
SoftwareSerial serialNode(RX, TX);
// --- VARIABLES DE TIEMPO ---

unsigned long tiempoAnterior = 0;
const unsigned long Ts_ms = 2;
const double Ts_sec = Ts_ms / 1000.0;

unsigned long tiempoAnteriorTelemetria = 0;
const unsigned long T_telemetria = 100;

// --- VARIABLES DEL SISTEMA ---

double r = 300.0;
double y = 0.0;
double e = 0.0;
double e_prev = 0.0;
double u = 0.0;
int pwmOut = 0;

// --- CONSTANTES DEL CONTROLADOR PI/PID ---

double Kp = 0.89;
double Ki = 59.33;
double Kd = 0.00;
double I_k = 0.0;

// --- VARIABLES DE OPERACIÓN ---
bool modoAuto = true;
double pwmManual = 0.0;
String bufferPC = "";

void setup() {
  pinMode(pinLED, OUTPUT);
  pinMode(pinLDR, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.begin(9600);
  serialNode.begin(9600);
  bufferPC.reserve(32);

  Serial.println("y,r,e,pwm,u");
}

void loop() {
  while (serialNode.available() > 0) {
    char c = serialNode.read();
    
    if (c == '\n' || c == '\r') {
      Serial.println("Primer iF");
      if (bufferPC.length() > 0) {
        Serial.println("PSegundo iF");
        procesarComando(bufferPC);
        bufferPC = "";
      }
    } else {
      bufferPC += c;
    }
  }

  unsigned long tiempoActual = millis();

  if (tiempoActual - tiempoAnterior >= Ts_ms) {
    tiempoAnterior += Ts_ms;

    y = analogRead(pinLDR);
    e = r - y;
    double de_raw = (e - e_prev) / Ts_sec;

    static double de = 0;
    de = 0.7 * de + 0.3 * de_raw;

    if (modoAuto) {
      u = (Kp * e) + (Ki * I_k) + (Kd * de);

      pwmOut = constrain((int)u, 0, 255);

      bool saturacionSuperior = (pwmOut >= 255 && e > 0);
      bool saturacionInferior = (pwmOut <= 0 && e < 0);

      if (!saturacionSuperior && !saturacionInferior) {
        I_k = I_k + e * Ts_sec;
        I_k = constrain(I_k, -1000, 1000);
      }

      u = (Kp * e) + (Ki * I_k)+ (Kd * de) ;
      

      u = constrain(u, 0, 255);

      pwmOut = (int)u;
      analogWrite(pinLED, pwmOut);

    } else {
      // Modo manual
      pwmOut = constrain((int)pwmManual, 0, 255);
      analogWrite(pinLED, pwmOut);

      u = pwmOut;
      
      if (Ki > 0.001) {
        I_k = ((double)pwmOut - (Kp * e) - (Kd * de)) / Ki;
        I_k = constrain(I_k, -1000, 1000); 
      }
    }
    e_prev = e;
  }

  if (tiempoActual - tiempoAnteriorTelemetria >= T_telemetria) {
    tiempoAnteriorTelemetria += T_telemetria;

    serialNode.print(y); serialNode.print(",");
    serialNode.print(r); serialNode.print(",");
    serialNode.print(e); serialNode.print(",");
    serialNode.println(u);
    
    Serial.print("PID -> y:"); Serial.print(y);
    Serial.print(" r:"); Serial.print(r);
    Serial.print(" e:"); Serial.print(e);
    Serial.print(" u:"); Serial.println(u);
  }

}

void procesarComando(String comando) {
  comando.trim();
  Serial.println("Comando desde NodeMCU: " + comando); 

  int indiceDosPuntos = comando.indexOf(':');
  if (indiceDosPuntos == -1) return;

  String clave = comando.substring(0, indiceDosPuntos);
  double valor = comando.substring(indiceDosPuntos + 1).toFloat();

  if (clave.equalsIgnoreCase("SP")) {
    r = valor;
  } 
  else if (clave.equalsIgnoreCase("KP")) {
    Kp = valor;
  } 
  else if (clave.equalsIgnoreCase("KI")) {
    Ki = valor;
  } 
  else if (clave.equalsIgnoreCase("MODO")) {
    modoAuto = ((int)valor == 1);
  } 
  else if (clave.equalsIgnoreCase("MAN")) {
    pwmManual = valor;
  }
  else if (clave.equalsIgnoreCase("KD")) {
    Kd = valor;
    e_prev = e; 
    
    if (Kd == 0) {
        I_k = 0; 
    }
  }
}
