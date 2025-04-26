#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <ArduinoJson.h>
#include <FirebaseESP8266.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <FS.h> // Para SPIFFS

// ------------------- Configuración Inicial -------------------
#define pinDatos 4 // D2 en D1 Mini (DHT22) GPI04
#define DIRECCION_BMP280 0x76 // Dirección I2C del BMP280

// Credenciales WiFi
const char* ssid = "POCO C65";
const char* password = "curupayti";
// Credenciales del WiFi Access Point
const char* ssidPropio = "DHT22BMP280";
const char* passwordPropio = "tl2arq2undav";

// Firebase
FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;
const String FIREBASE_RUTA = "/lecturas/";

// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -10800); // UTC-3 (Argentina)

// Sensores
DHT dht(pinDatos, DHT22);
Adafruit_BMP280 bmp;

// Variables globales
float tempDHT, humDHT, tempBMP, presionBMP;
unsigned long tiempoUltimaLectura = 0;
const unsigned long intervaloLectura = 60000; // 1 minuto
bool wifiConectadoAnterior = false;

// Web Server
ESP8266WebServer server(80);

// ------------------- HTML Simplificado (PROGMEM) -------------------
const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Sensores Local</title>
  <meta http-equiv="refresh" content="5">
  <style>
    body { font-family: Arial; text-align: center; padding: 20px; }
    .datos { margin: 10px; padding: 10px; background: #f0f0f0; }
  </style>
</head>
<body>
  <h1>Datos en Tiempo Real (Offline)</h1>
  <div class="datos">
    <p>Temp DHT22: %TEMPDHT% °C</p>
    <p>Humedad DHT22: %HUMDHT% %%</p>
    <p>Temp BMP280: %TEMPBMP% °C</p>
    <p>Presión BMP280: %PRESBMP% hPa</p>
  </div>
  <div class="info">
    <p>Conectado vía: %MODE%</p>
    <p>IP STA: %IPSTA%</p>
    <p>IP AP: 192.168.4.1</p>
  </div>
</body>
</html>
)rawliteral";

// ------------------- Funciones Clave -------------------

void setup() {
  Serial.begin(115200);
  delay(3000);

  // Iniciar sensores
  dht.begin();
  Wire.begin(12, 14); // SDA=GPIO12 (D6), SCL=GPIO14 (D5)
  if (!bmp.begin(DIRECCION_BMP280)) {
    Serial.println("Error BMP280. Reiniciando...");
    ESP.restart();
  }

  // Conectar WiFi
  conectarWiFi();

  // Iniciar NTP y sincronizar
  timeClient.begin();
  sincronizarNTP();

  // Firebase
  configurarFirebase();
  autenticarUsuario();

  // Iniciar SPIFFS
  SPIFFS.begin();

  // Configurar rutas del servidor web
  server.on("/", HTTP_GET, mostrarPaginaLocal);
  server.on("/datos", HTTP_GET, enviarDatosJSON);
  server.begin();
  // De forma manual crear API
  server.on("/ap", HTTP_GET, []() {
    server.send(200, "text/html", "<h1>Configuración AP</h1><p>SSID: DHT22+BMP280</p><p>Contraseña: tl2arq2</p>");
  });
}
void loop() {
  server.handleClient();
  unsigned long ahora = millis();

  // Verificar conexión WiFi
  bool wifiConectado = (WiFi.status() == WL_CONNECTED);
  if (wifiConectado && !wifiConectadoAnterior) {
    sincronizarNTP();
    sincronizarSPIFFS(); // Enviar datos pendientes
  }
  wifiConectadoAnterior = wifiConectado;

  // Lógica de lectura/envió
  if (ahora - tiempoUltimaLectura >= intervaloLectura) {
    tiempoUltimaLectura = ahora;
    leerSensores();

    if (wifiConectado && esHoraValida()) {
      enviarDatosAFirebase(obtenerFechaHora());
    } else {
      guardarEnSPIFFS();
    }
  }
}

// ------------------- Implementación de Funciones -------------------

void conectarWiFi() {
  // --- Reset total del WiFi ---
  WiFi.persistent(false);  // Evita guardar config en flash
  WiFi.disconnect(true);   // Borra configuraciones anteriores
  delay(1000);
  WiFi.mode(WIFI_OFF);     // Apaga completamente el módulo
  delay(1000);

  // --- Configurar AP con parámetros forzados ---
  WiFi.mode(WIFI_AP_STA);
  

 
  // Configuración EXPLÍCITA de parámetros
  bool apStatus = WiFi.softAP(
    ssidPropio,  // SSID (máx 32 caracteres)
    passwordPropio,        // Contraseña (8-63 caracteres)
    1,                // Canal WiFi (1-13)
    false,            // SSID oculto (false = visible)
    8                 // Máximo de conexiones
  );

  // --- Configuración IP (usar rango estándar ESP) ---
  WiFi.softAPConfig(
    IPAddress(192, 168, 4, 1), // IP clásica de ESP AP
    IPAddress(192, 168, 4, 1),
    IPAddress(255, 255, 255, 0)
  );

  // Debug en Monitor Serial
  Serial.println("\n--- Estado del AP ---");
  Serial.print("SSID configurado: "); Serial.println(WiFi.softAPSSID());
  Serial.print("Contraseña: "); Serial.println(WiFi.softAPPSK());
  Serial.print("Canal: "); Serial.println(WiFi.channel());
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());
  Serial.println(apStatus ? "AP creado correctamente" : "Fallo al crear AP");

  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi...");
  for (int i = 0; i < 20; i++) {
    if (WiFi.status() == WL_CONNECTED) break;
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nError al conectar.");
    Serial.println("\nModo offline: Solo AP Local");
  } else {
    Serial.println("\nConectado. IP: " + WiFi.localIP().toString());
  }
}

void configurarFirebase() {
  config.api_key = "AIzaSyBUF-oj6rDEzZFlttKXjzUBY4OaVQYNtLE";
  config.database_url = "https://arq2-tl2-temperatura-humedad-default-rtdb.firebaseio.com/";
  auth.user.email = "medinadaniloantonio1@gmail.com";
  auth.user.password = "tl2arq2";
  Firebase.reconnectWiFi(true);
}

void autenticarUsuario() {
  Firebase.begin(&config, &auth);
  unsigned long inicio = millis();
  while (!Firebase.ready() && (millis() - inicio < 15000)) {
    delay(100);
    Serial.print(".");
  }
  if (Firebase.ready()) {
    Serial.println("\nAutenticado en Firebase. Token válido por 1 hora.");
  } else {
    Serial.println("\nError de autenticación: " + firebaseData.errorReason());
  }
}
void sincronizarNTP() {
  timeClient.forceUpdate();
  setTime(timeClient.getEpochTime());
  Serial.println("Hora sincronizada: " + obtenerFechaHora());
}

bool esHoraValida() {
  return (year() >= 2025 && month() >= 1 && month() <= 12);
}

String obtenerFechaHora() {
  return String(year()) + "-" + 
         twoDigits(month()) + "-" + 
         twoDigits(day()) + "-" + 
         twoDigits(hour()) + "-" + 
         twoDigits(minute()) + "-" + 
         twoDigits(second());
}

String twoDigits(int number) {
  return (number < 10) ? "0" + String(number) : String(number);
}

void leerSensores() {
  tempDHT = dht.readTemperature();
  humDHT = dht.readHumidity();
  tempBMP = bmp.readTemperature();
  presionBMP = bmp.readPressure() / 100.0F;
}

void enviarDatosAFirebase(const String &timestamp) {
  //Si se está sin conexión a internet STA. 
  if (WiFi.status() != WL_CONNECTED) {  // Solo intentar si hay conexión STA
    Serial.println("Modo AP: No se intenta Firebase");
    guardarEnSPIFFS();
    return;
  }  
  // Verificar si el token está expirado
  if (Firebase.isTokenExpired()) {
    Serial.println("Token expirado. Refrescando...");
    Firebase.refreshToken(&config); // Intenta renovar con el refresh token
    delay(500);
  }

  // Si el token sigue inválido, reautenticar
  if (!Firebase.ready()) {
    Serial.println("Error: Token no válido. Reautenticando...");
    autenticarUsuario();
  }

  // Enviar datos solo si Firebase está listo
  if (Firebase.ready()) {
    FirebaseJson json;
    json.set("tempDHT", tempDHT);
    json.set("humDHT", humDHT);
    json.set("tempBMP", tempBMP);
    json.set("presionBMP", presionBMP);

    if (Firebase.setJSON(firebaseData, FIREBASE_RUTA + timestamp, json)) {
      Serial.println("Datos enviados a Firebase.");
    } else {
      Serial.println("Error al enviar: " + firebaseData.errorReason());
      guardarEnSPIFFS(); // Guardar en SPIFFS si falla el envío
    }
  } else {
    Serial.println("Error grave: No se pudo autenticar con Firebase.");
    guardarEnSPIFFS();
  }
}

void guardarEnSPIFFS() {
  File file = SPIFFS.open("/pendientes.txt", "a");
  if (file) {
    String linea = obtenerFechaHora() + "," + 
                  String(tempDHT) + "," + 
                  String(humDHT) + "," + 
                  String(tempBMP) + "," + 
                  String(presionBMP) + "\n";
    file.print(linea);
    file.close();
    Serial.println("Datos guardados en SPIFFS.");
  }
}

void sincronizarSPIFFS() {
  File file = SPIFFS.open("/pendientes.txt", "r");
  if (!file) return;

  while (file.available()) {
    String linea = file.readStringUntil('\n');
    String timestamp = linea.substring(0, linea.indexOf(','));
    enviarDatosAFirebase(timestamp);
    delay(100); // Evitar saturación
  }
  file.close();
  SPIFFS.remove("/pendientes.txt");
}

void mostrarPaginaLocal() {
  String html = FPSTR(HTML);
  html.replace("%TEMPDHT%", String(tempDHT, 1));
  html.replace("%HUMDHT%", String(humDHT, 1));
  html.replace("%TEMPBMP%", String(tempBMP, 1));
  html.replace("%PRESBMP%", String(presionBMP, 1));
  html.replace("%MODE%", WiFi.status() == WL_CONNECTED ? "WiFi + Internet" : "AP Local (Offline)");
  html.replace("%IPSTA%", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "No conectado");
  server.send(200, "text/html", html);
}

void enviarDatosJSON() {
  String json = "{";
  json += "\"temp_dht\":" + String(tempDHT, 1) + ",";
  json += "\"hum_dht\":" + String(humDHT, 1) + ",";
  json += "\"temp_bmp\":" + String(tempBMP, 1) + ",";
  json += "\"presion_bmp\":" + String(presionBMP, 1);
  json += "}";
  server.send(200, "application/json", json);
}