// --- PINES FÍSICOS ---
const int pinLDR = A0;
const int pinLED = 3;

// --- VARIABLES DE TIEMPO ---
unsigned long tiempoAnterior = 0;
const unsigned long Ts_ms = 2;
const double Ts_sec = Ts_ms / 1000.0;
double Ti = 0.002*2.605;
unsigned long tiempoAnteriorTelemetria = 0;
const unsigned long T_telemetria = 100;

// --- VARIABLES DEL SISTEMA ---
double r = 300.0;
double y = 0.0;
double e = 0.0;
double u = 0.0;
int pwmOut = 0;

// --- CONSTANTES DEL CONTROLADOR PI ---
double Kp = 0.89;
double Ki = 59.33;
double I_k = 0.0;

// --- VARIABLES DE OPERACIÓN ---
bool modoAuto = true;
double pwmManual = 0.0;
String bufferPC = "";

void setup() {
  pinMode(pinLED, OUTPUT);
  pinMode(pinLDR, INPUT);

  Serial.begin(115200);
  bufferPC.reserve(32);

  // Si usás Serial Plotter, conviene NO imprimir mucho texto al inicio.
  // Usá el Monitor Serie para comandos.
  Serial.println("y,r,e,pwm,u");
}

void loop() {
  // 1. RECEPCIÓN DE COMANDOS
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (bufferPC.length() > 0) {
        procesarComando(bufferPC);
        bufferPC = "";
      }
    } else {
      bufferPC += c;
    }
  }

  unsigned long tiempoActual = millis();

  // 2. CONTROLADOR DISCRETO CADA Ts_ms
  if (tiempoActual - tiempoAnterior >= Ts_ms) {
    tiempoAnterior += Ts_ms;

    y = analogRead(pinLDR);
    e = r - y;

    if (modoAuto) {
      // Cálculo preliminar de control
      u = (Kp * e) + (Ki * I_k);

      // Saturación física del PWM
      pwmOut = constrain((int)u, 0, 255);

      // Anti-windup por clamping
      bool saturacionSuperior = (pwmOut >= 255 && e > 0);
      bool saturacionInferior = (pwmOut <= 0 && e < 0);

      if (!saturacionSuperior && !saturacionInferior) {
        I_k = I_k + e * Ts_sec;
      }

      // Recalcular u luego de actualizar la integral
      u = (Kp * e) + (Ki * I_k);
      pwmOut = constrain((int)u, 0, 255);

      analogWrite(pinLED, pwmOut);

    } else {
      // Modo manual
      pwmOut = constrain((int)pwmManual, 0, 255);
      analogWrite(pinLED, pwmOut);

      u = pwmOut;

      // Bumpless transfer correcto:
      // u = Kp*e + Ki*I_k
      // I_k = (u - Kp*e) / Ki
      if (Ki != 0) {
        I_k = ((double)pwmOut - Kp * e) / Ki;
      }
    }
  }

  
  // 3. TELEMETRÍA PARA SERIAL PLOTTER
  if (tiempoActual - tiempoAnteriorTelemetria >= T_telemetria) {
    tiempoAnteriorTelemetria += T_telemetria;

    if (Ki != 0) {
      Ti = Kp / Ki;
    } else {
      Ti = 0;
    }

    Serial.print(y);
    Serial.print(",");
    Serial.print(r);
    Serial.print(",");
    Serial.print(e);
    Serial.print(",");
    Serial.print(pwmOut);
    Serial.print(",");
    Serial.print(u);
    Serial.print(",");
    Serial.println(Ti);
  }

}

// --- PROCESAMIENTO DE COMANDOS ---
void procesarComando(String comando) {
  comando.trim();

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
}
