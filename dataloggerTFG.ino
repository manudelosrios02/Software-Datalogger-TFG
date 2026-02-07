/*
  ---------------------------------------------------------------------------
  Proyecto:  DISEÑO Y CONSTRUCCIÓN DE MÓDULO DE MEDICIÓN Y MONITOREO 
             PARA INSTALACIÓN FOTOVOLTAICA AUTÓNOMA
  Autor:     Manuel de los Ríos Piosa
  Versión:   3.0
  Fecha:     14/01/2026

  Descripción:
    Este programa realiza la adquisición, registro y almacenamiento de 
    medidas eléctricas de tensión, corriente, potencia y energía procedentes 
    de dos sensores de monitorización INA226 y un medidor de energía 
    PZEM-004T v3.0.  

    Los datos se almacenan en una tarjeta microSD en formato CSV, generando 
    archivos numerados automáticamente para evitar el solapamiento entre 
    distintas sesiones de medida.

    La comunicación con los sensores se realiza mediante los buses I2C 
    (INA226) y UART (PZEM-004T), mientras que la tarjeta microSD utiliza el 
    bus SPI. El sistema está preparado para futuras ampliaciones de 
    conectividad mediante WiFi.

  Hardware utilizado:
    - Arduino UNO R4 (Renesas RA4M1)
    - Sensores INA226 (x2) – Comunicación I2C
    - Medidor de energía PZEM-004T v3.0 – Comunicación UART (Serial1)
    - Módulo microSD – Comunicación SPI

  Librerías utilizadas y créditos:
    * INA226 Library
        Autor: Rob Tillaart
        URL:   https://github.com/RobTillaart/INA226
        Licencia: MIT License

    * PZEMPlus Library
        Autor: Oleksandr "olehs"
        URL:   https://github.com/olehs/PZEM004T
        Licencia: GNU General Public License v3.0

    * SD Library (Arduino)
        Autor: Arduino / SparkFun / SdFat backend
        Licencia: LGPL 2.1 / BSD

    * Wire.h
        Autor: Arduino
        Licencia: LGPL 2.1

    * SPI.h
        Autor: Arduino
        Licencia: LGPL 2.1

    * WiFiS3 Library
        Autor: Arduino
        Licencia: LGPL 2.1

  Notas importantes:
    - La precisión de las medidas del INA226 depende de la configuración del
      registro de calibración y de la resolución del ADC interno.
    - El medidor PZEM-004T proporciona medidas de energía acumulada de forma
      autónoma, con resolución determinada por el propio dispositivo.
    - Este software se distribuye sin garantía; su uso queda bajo la 
      responsabilidad del usuario y debe respetar las licencias de cada 
      librería empleada.

  ---------------------------------------------------------------------------
*/

#include <Wire.h>
#include <SPI.h> 
#include <SD.h>
#include <INA226.h>

#define PZEM_004T
#include <PZEMPlus.h>

#include <WiFiS3.h>

// ==================== INA226 ====================
INA226 ina1(0x40);
INA226 ina2(0x41);

const uint8_t NUMERO_MUESTRAS    = 4; // 1,4,16,64,... ; 4 = 64 muestras
const uint8_t TIEMPO_CONVERSION  = 7; // 0–7

const float RES_SHUNT_INA1 = 0.0012;
const float I_MAX_INA1     = 40.0;

const float RES_SHUNT_INA2 = 0.0012;
const float I_MAX_INA2     = 20.0;

const float R_EQ = 0.026f; // resistencia del cableado
const float corrector_offset = 12.74/12.908;
float correctVoltage(float vbus, float currentA) {
  return vbus - currentA * R_EQ;
}

// ==================== PZEM ====================
PZEMPlus pzem(Serial1);

// ==================== SD ====================
const int SD_CS_PIN = 10;

// ==================== WiFi AP (UNO R4 WiFi) ====================
char ssid[] = "datalogger_IES";
char pass[] = "datalogger_IES";
WiFiServer server(80);

// ==================== Máquina de estados ====================
// 0 = Menú
// 1 = Pidiendo nombre de archivo
// 2 = Leer archivo y volcar por Serial
// 3 = Borrar archivo
// 4 = Sesión de medida activa
int estado = 0;

// ==================== Logging ====================
String nombreArchivo = "";   // nombre final (8.3) con .CSV
File   logFile;
File   root;

const unsigned long PERIODO_LOG_MS = 1000;
unsigned long lastLoop = 0;
unsigned long tiempo_s = 0;

// ==================== Consola Web ====================
String consola = "";
String ultimaLineaCSV = "";   
unsigned long lastWebPushMs = 0;   // para actualizar ultimaLineaCSV cada ~10s
const unsigned long PERIODO_WEB_MS = 1000; // periodo de actualización

// -------------------- LOG: Serial + Consola web --------------------
void logMsg(const String &msg) {
  Serial.println(msg);
  consola += msg + "\n";

  const int MAX_LEN = 6000; // logs + datos
  if (consola.length() > MAX_LEN) {
    consola = consola.substring(consola.length() - MAX_LEN);
  }
}

// -------------------- Helpers nombre archivo --------------------
bool nombreValidoBasico(const String &nombre) {
  if (nombre.length() == 0) return false;
  if (nombre[0] == '.') return false;

  const char invalidos[] = "/\\:*?\"<>|";
  for (int i = 0; i < nombre.length(); i++) {
    for (int j = 0; j < (int)sizeof(invalidos) - 1; j++) {
      if (nombre[i] == invalidos[j]) return false;
    }
  }
  return true;
}

// Convierte a un nombre base 8.3: [A-Z0-9_] (máx 8)
String to83BaseName(String in) {
  in.trim();
  in.toUpperCase();

  String out = "";
  out.reserve(8);

  for (int i = 0; i < in.length() && out.length() < 8; i++) {
    char c = in[i];

    // Mantener dígitos y letras
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      out += c;
    }
    // Convertir separadores comunes a '_'
    else if (c == '-' || c == ' ' || c == '.') {
      out += '_';
    }
    // Ignorar el resto
  }

  // Si queda vacío, usar un nombre por defecto
  if (out.length() == 0) out = "LOG";

  return out;
}

String buildCsvFilenameFromBase(String base) {
  String b = to83BaseName(base);
  return b + ".CSV";
}

// -------------------- Menú Serial --------------------
void mostrarMenu() {
  // Limpiar Serial Monitor
  Serial.println();
  for (int i = 0; i < 5; i++) {
    Serial.println();
  }
  Serial.println(F("Bienvenido al asistente del Datalogger"));
  Serial.println(F("Para elegir un comando de la lista, escribe la letra en minúscula. Por ejemplo: a"));
  Serial.println(F("a) Iniciar sesión de medida"));
  Serial.println(F("b) Leer un archivo en el PC"));
  Serial.println(F("c) Borrar un archivo"));
  Serial.println(F("Durante una sesión de medida, escriba 'back' para detener y volver al menú"));
  Serial.println();
}

// -------------------- Listado de SD por Serial --------------------
void printDirectory(File dir, int numTabs) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    for (uint8_t i = 0; i < numTabs; i++) Serial.print('\t');
    Serial.print(entry.name());

    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}

// ==================== Iniciar / parar sesión ====================
bool iniciarSesionMedida(const String &baseName, const String &origen) {
  if (!nombreValidoBasico(baseName)) {
    logMsg(origen + ": ERROR nombre inválido.");
    return false;
  }

  // Si ya hay una sesión, no iniciar otra
  if (estado == 4 && logFile) {
    logMsg(origen + ": Ya hay una sesión activa. Para antes la sesión.");
    return false;
  }

  nombreArchivo = buildCsvFilenameFromBase(baseName);

  logMsg(origen + ": Nombre solicitado = " + baseName);
  logMsg(origen + ": Nombre final (8.3) = " + nombreArchivo);

  logFile = SD.open(nombreArchivo.c_str(), FILE_WRITE);
  if (!logFile) {
    logMsg(origen + ": ERROR no se pudo crear/abrir el archivo en SD.");
    estado = 0;
    return false;
  }

  // Cabecera CSV
  logFile.println(F("t_s,VBUS1,VSHUNT1,I1,P1,VBUS2,VSHUNT2,I2,P2,VPZ,IPZ,PPZ,EPZ,FPZ,PF_PZ"));
  logFile.flush();

  tiempo_s = 0;
  lastLoop = millis();
  estado = 4;

  logMsg(origen + ": Sesión iniciada. Escriba 'back' o use STOP en la web.");
  return true;
}

void pararSesionMedida(const String &origen) {
  if (estado == 4) {
    logMsg(origen + ": Deteniendo sesión de medida...");
    if (logFile) logFile.close();
    estado = 0;
    mostrarMenu();
  } else {
    logMsg(origen + ": No hay sesión activa.");
  }
}

// ==================== WEB: HTML (blanco/negro) ====================
void sendMainPage(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=utf-8");
  client.println("Connection: close");
  client.println();
  client.println("<!DOCTYPE html><html><head><meta charset='utf-8'>");
  client.println("<title>Datalogger UNO R4</title>");

  // Estilo blanco/negro
  client.println("<style>");
  client.println("body{font-family:monospace;background:#fff;color:#000;margin:18px;}");
  client.println("h2,h3{color:#000;margin:0 0 10px 0;}");
  client.println("#top{display:flex;gap:12px;align-items:flex-start;}");
  client.println("#controls{background:#f5f5f5;border:1px solid #000;padding:10px;min-width:320px;}");
  client.println("#console{white-space:pre-wrap;background:#f5f5f5;border:1px solid #000;padding:10px;height:55vh;overflow-y:scroll;flex:1;}");
  client.println("#files{background:#f5f5f5;border:1px solid #000;padding:10px;margin-top:12px;}");
  client.println("a{color:#0000ee;text-decoration:none;}");
  client.println("a:hover{text-decoration:underline;}");
  client.println("button{background:#e0e0e0;color:#000;border:1px solid #000;padding:4px 8px;cursor:pointer;}");
  client.println("button:hover{background:#d0d0d0;}");
  client.println("input{background:#fff;color:#000;border:1px solid #000;padding:3px;width:180px;}");
  client.println(".small{font-size:12px;color:#222;}");
  client.println("#console table{border-collapse:collapse;width:100%;}");
  client.println("#console td{border:1px solid #000;padding:6px;}");
  client.println("#console td:first-child{width:55%;}");
  client.println("</style>");

  client.println("</head><body>");
  client.println("<h2>Datalogger UNO R4 WiFi</h2>");

  client.println("<div id='top'>");

  // Controles
  client.println("<div id='controls'>");
  client.println("<h3>Controles</h3>");
  client.println("<div>Nombre base: <input id='fname' placeholder='díamesaño'></div>");
  client.println("<div style='margin-top:8px;'>");
  client.println("<button onclick='startLogging()'>Iniciar Medida</button> ");
  client.println("<button onclick='stopLogging()'>Detener Medida</button>");
  client.println("</div>");
  client.println("</div>");

  // Consola
  client.println("<div style='flex:1;'>");
  client.println("<h3>Consola</h3>");
  client.println("<div id='console'>");
  client.println("<table id='t'>");
  client.println("<tr><td>t (s)</td><td id='t_s'>-</td></tr>");
  client.println("<tr><td>Tensión FV (V)</td><td id='v1'>-</td></tr>");
  client.println("<tr><td>Corriente FV (A)</td><td id='i1'>-</td></tr>");
  client.println("<tr><td>Potencia FV (W)</td><td id='p1'>-</td></tr>");
  client.println("<tr><td>Tensión Batería (V)</td><td id='v2'>-</td></tr>");
  client.println("<tr><td>Corriente Batería (A)</td><td id='i2'>-</td></tr>");
  client.println("<tr><td>Potencia Batería (W)</td><td id='p2'>-</td></tr>");
  client.println("<tr><td>Corriente AC (A)</td><td id='ipz'>-</td></tr>");
  client.println("<tr><td>Potencia AC (W)</td><td id='ppz'>-</td></tr>");
  client.println("</table>");
  client.println("</div>");

  client.println("</div>");

  client.println("</div>"); // top

  // Archivos
  client.println("<div id='files'>");
  client.println("<h3>Archivos en la SD</h3>");

  File r = SD.open("/");
  if (!r) {
    client.println("<b>Error al abrir la raíz de la SD.</b>");
  } else {
    File e;
    bool any = false;
    while ((e = r.openNextFile())) {
      any = true;
      String name = e.name();
      client.print("<div>");
      client.print(name);
      client.print(" &nbsp; ");
      client.print("<a href='/download?name=");
      client.print(name);
      client.print("'>[Descargar]</a> &nbsp; ");

      // Botón borrar
      client.print("<a href='#' onclick=\"deleteFile('");
      client.print(name);
      client.print("');return false;\">[Borrar]</a>");
      client.println("</div>");
      e.close();
    }
    r.close();
    if (!any) client.println("<div class='small'>(No hay archivos)</div>");
  }

  client.println("</div>"); // files

  // JS
  client.println("<script>");
  client.println("function updateLast(){");
  client.println("  fetch('/last').then(r=>r.text()).then(t=>{");
  client.println("    const a = t.trim().split(',');");
  client.println("    if(a.length < 9) return;");
  client.println("    document.getElementById('t_s').textContent = a[0];");
  client.println("    document.getElementById('v1').textContent  = a[1];");
  client.println("    document.getElementById('i1').textContent  = a[2];");
  client.println("    document.getElementById('p1').textContent  = a[3];");
  client.println("    document.getElementById('v2').textContent  = a[4];");
  client.println("    document.getElementById('i2').textContent  = a[5];");
  client.println("    document.getElementById('p2').textContent  = a[6];");
  client.println("    document.getElementById('ipz').textContent = a[7];");
  client.println("    document.getElementById('ppz').textContent = a[8];");
  client.println("  }).catch(_=>{});");
  client.println("}");
  client.println("setInterval(updateLast, 10000); updateLast();");

  client.println("function startLogging(){");
  client.println("  const n=document.getElementById('fname').value.trim();");
  client.println("  if(!n){ alert('Introduce un nombre'); return; }");
  client.println("  fetch('/cmd?op=start&name='+encodeURIComponent(n))");
  client.println("    .then(r=>r.text()).then(_=>{ setTimeout(()=>location.reload(), 300); });");
  client.println("}");

  client.println("function stopLogging(){");
  client.println("  fetch('/cmd?op=stop').then(r=>r.text()).then(_=>{ setTimeout(()=>location.reload(), 300); });");
  client.println("}");

  client.println("function deleteFile(n){");
  client.println("  if(!confirm('¿Borrar '+n+'?')) return;");
  client.println("  fetch('/cmd?op=delete&name='+encodeURIComponent(n))");
  client.println("    .then(r=>r.text()).then(_=>{ setTimeout(()=>location.reload(), 300); });");
  client.println("}");

  client.println("</script>");

  client.println("</body></html>");
}

void sendLogData(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/plain; charset=utf-8");
  client.println("Connection: close");
  client.println();
  client.print(consola);
}

void sendLastData(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/plain; charset=utf-8");
  client.println("Connection: close");
  client.println();
  client.print(ultimaLineaCSV);
}

void sendNotFound(WiFiClient &client) {
  // Redirige cualquier URL desconocida a la página principal "/"
  client.println("HTTP/1.1 302 Found");
  client.println("Location: /");
  client.println("Connection: close");
  client.println();
}

void sendFileDownload(WiFiClient &client, const String &filename) {
  File f = SD.open(filename.c_str(), FILE_READ);
  if (!f) {
    client.println("HTTP/1.1 404 Not Found");
    client.println("Content-Type: text/plain; charset=utf-8");
    client.println("Connection: close");
    client.println();
    client.println("Archivo no encontrado");
    return;
  }

  client.println("HTTP/1.1 200 OK");
  client.print("Content-Disposition: attachment; filename=\"");
  client.print(filename);
  client.println("\"");
  client.println("Content-Type: application/octet-stream");
  client.println("Connection: close");
  client.println();

  char buf[256];
  while (f.available()) {
    int n = f.readBytes(buf, sizeof(buf));
    client.write((const uint8_t*)buf, n);
  }
  f.close();
}

// Procesa /cmd?op=...
void processWebCommand(const String &op, const String &name) {
  if (op == "start") {
    iniciarSesionMedida(name, "WEB");
  }
  else if (op == "stop") {
    pararSesionMedida("WEB");
  }
  else if (op == "delete") {
    String fname = name;
    fname.trim();
    if (fname.length() == 0) return;

    // No permitir borrar el archivo en uso
    if (estado == 4 && nombreArchivo.length() > 0 && fname.equalsIgnoreCase(nombreArchivo)) {
      logMsg("WEB: No se puede borrar el archivo en uso (" + nombreArchivo + "). Para la sesión primero.");
      return;
    }

    logMsg("WEB: Borrando " + fname);
    if (SD.remove(fname.c_str())) logMsg("WEB: Borrado OK");
    else logMsg("WEB: Error al borrar");
  }
  else {
    logMsg("WEB: Comando desconocido: " + op);
  }
}

// Router HTTP
void handleWifiClient() {
  WiFiClient client = server.available();
  if (!client) return;

  unsigned long start = millis();
  while (!client.available() && millis() - start < 800) delay(1);
  if (!client.available()) { client.stop(); return; }

  String req = client.readStringUntil('\r');
  client.readStringUntil('\n');

  String path = "/";
  if (req.startsWith("GET ")) {
    int a = req.indexOf(' ');
    int b = req.indexOf(' ', a + 1);
    if (a > 0 && b > a) path = req.substring(a + 1, b);
  }

  while (client.available()) client.read();

  if (path == "/") {
    sendMainPage(client);
  } else if (path == "/log") {
    sendLogData(client);
  } else if (path == "/last") {
  sendLastData(client);
  } else if (path.startsWith("/download?name=")) {
    String filename = path.substring(String("/download?name=").length());
    sendFileDownload(client, filename);
  } else if (path.startsWith("/cmd?")) {
    // Parse op y name
    String query = path.substring(path.indexOf('?') + 1);

    String op = "", name = "";
    int opPos = query.indexOf("op=");
    if (opPos >= 0) {
      int amp = query.indexOf('&', opPos);
      op = (amp >= 0) ? query.substring(opPos + 3, amp) : query.substring(opPos + 3);
    }
    int namePos = query.indexOf("name=");
    if (namePos >= 0) {
      int amp = query.indexOf('&', namePos);
      name = (amp >= 0) ? query.substring(namePos + 5, amp) : query.substring(namePos + 5);
    }

    processWebCommand(op, name);

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain; charset=utf-8");
    client.println("Connection: close");
    client.println();
    client.println("OK");
  } else {
    sendNotFound(client);
  }

  delay(1);
  client.stop();
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(9600);
  Wire.begin();

  while (!Serial) { }
  delay(500);

  Serial.println("DEBUG: setup() iniciado");

  lastLoop = millis();
  tiempo_s = 0;

  // INA226 #1
  if (!ina1.begin()) {
    logMsg("Error: INA226 #1 no responde");
  } else {
    ina1.setAverage(NUMERO_MUESTRAS);
    ina1.setShuntVoltageConversionTime(TIEMPO_CONVERSION);
    ina1.setBusVoltageConversionTime(TIEMPO_CONVERSION);
    ina1.setMode(7);
    ina1.setMaxCurrentShunt(I_MAX_INA1, RES_SHUNT_INA1, true);
    logMsg("INA226 #1 inicializado");
  }

  // INA226 #2
  if (!ina2.begin()) {
    logMsg("Error: INA226 #2 no responde");
  } else {
    ina2.setAverage(NUMERO_MUESTRAS);
    ina2.setShuntVoltageConversionTime(TIEMPO_CONVERSION);
    ina2.setBusVoltageConversionTime(TIEMPO_CONVERSION);
    ina2.setMode(7);
    ina2.setMaxCurrentShunt(I_MAX_INA2, RES_SHUNT_INA2, true);
    logMsg("INA226 #2 inicializado");
  }

  // SD
  if (!SD.begin(SD_CS_PIN)) {
    logMsg("Error: no se pudo inicializar la tarjeta SD");
  } else {
    logMsg("Tarjeta SD inicializada correctamente");
  }

  // PZEM
  logMsg("Inicializando PZEM (PZEMPlus)...");
  Serial1.begin(9600);
  pzem.begin();
  logMsg("PZEMPlus: begin() llamado");

  // WiFi AP + servidor
  logMsg("Creando Access Point WiFi...");
  int status = WiFi.beginAP(ssid, pass);
  if (status != WL_AP_LISTENING) {
    logMsg("FALLO al crear el AP WiFi.");
    while (true) {}
  }
  IPAddress ip = WiFi.localIP();
  logMsg("AP creado. SSID: " + String(ssid));
  logMsg("Abre en el navegador: http://" + ip.toString());

  server.begin();
  logMsg("Servidor HTTP iniciado.");

  Serial.println("DEBUG: setup() terminado, mostrando menú");
  mostrarMenu();
}

// ==================== LOOP ====================
void loop() {
  // 1) Atender web siempre (incluso durante logging)
  handleWifiClient();

  // 2) Interfaz por Serial
  if (Serial.available() > 0) {
    String entrada = Serial.readStringUntil('\n');
    entrada.trim();

    // ESTADO 0
    if (estado == 0) {
      if (entrada == "a") {
        logMsg("Elige un nombre base para el archivo (se añadirá .CSV). Ej: 11-12-2025");
        estado = 1;
      }
      else if (entrada == "b") {
        logMsg("Escribe el nombre del archivo que quieres volcar por Serial (incluye .CSV):");
        root = SD.open("/");
        printDirectory(root, 0);
        if (root) root.close();
        estado = 2;
      }
      else if (entrada == "c") {
        logMsg("Escribe el nombre del archivo que quieres borrar (incluye .CSV):");
        estado = 3;
      }
      else {
        logMsg("FALLO: comando no reconocido.");
      }
    }

    // ESTADO 1: iniciar logging (pidiendo nombre)
    else if (estado == 1) {
      if (entrada == "back") {
        estado = 0;
        mostrarMenu();
      } else {
        // Inicia sesión (esto pasa a estado 4 internamente si ok)
        if (iniciarSesionMedida(entrada, "SERIAL")) {
          // ya estamos en estado 4
        } else {
          // si falla, volver al menú
          estado = 0;
          mostrarMenu();
        }
      }
    }

    // ESTADO 2: leer archivo y volcar por Serial
    else if (estado == 2) {
      if (entrada == "back") {
        estado = 0;
        mostrarMenu();
      } else {
        File f = SD.open(entrada.c_str(), FILE_READ);
        if (!f) {
          logMsg("ERROR: no se pudo abrir el archivo para lectura.");
        } else {
          logMsg("----- INICIO DEL ARCHIVO -----");
          while (f.available()) Serial.write(f.read());
          logMsg("----- FIN DEL ARCHIVO -----");
          f.close();
        }
        logMsg("Escribe 'back' para volver al menú o el nombre de otro archivo para leerlo.");
      }
    }

    // ESTADO 3: borrar
    else if (estado == 3) {
      if (entrada == "back") {
        estado = 0;
        mostrarMenu();
      } else {
        // no permitir borrar el archivo en uso
        if (logFile && nombreArchivo.length() > 0 && entrada.equalsIgnoreCase(nombreArchivo)) {
          logMsg("ERROR: No puedes borrar el archivo en uso. Para la sesión primero.");
        } else {
          if (SD.remove(entrada.c_str())) {
            logMsg("Archivo borrado: " + entrada);
          } else {
            logMsg("ERROR: no se pudo borrar el archivo: " + entrada);
          }
        }
        logMsg("Escribe 'back' para volver al menú o el nombre de otro archivo para borrar.");
      }
    }

    // ESTADO 4: sesión activa
    else if (estado == 4) {
      if (entrada == "back") {
        pararSesionMedida("SERIAL");
      } else {
        logMsg("Sesión activa. Escriba 'back' para detener.");
      }
    }
  }

  // 3) LOGGING periódico SOLO en estado 4
  if (estado == 4 && logFile) {
    unsigned long ahora = millis();
    if (ahora - lastLoop >= PERIODO_LOG_MS) {
      lastLoop += PERIODO_LOG_MS;
      tiempo_s++;

      // INA1
      float busVoltage1_SC   = ina1.getBusVoltage() * 4; // Sin Calibrar
      float shuntVoltage1 = ina1.getShuntVoltage();
      float current1_A    = ina1.getCurrent();
      float power1_W      = ina1.getPower() * 4;
      float busVoltage1 = correctVoltage(busVoltage1_SC, current1_A); // Calibrada
   

      // INA2
      float busVoltage2_SC   = ina2.getBusVoltage(); //Usar esta medida sin calibrar introduce errores del ~4% debido a la caída de tensión.
      float shuntVoltage2 = ina2.getShuntVoltage();
      float current2_A    = ina2.getCurrent();
      float power2_W      = ina2.getPower();
      float busVoltage2 = correctVoltage(busVoltage2_SC, current2_A) * corrector_offset; // Calibrada

      // PZEM
      float voltaje, corriente, potencia, energia, frecuencia, pf;
      bool okPZEM = pzem.readAll(&voltaje, &corriente, &potencia, &energia, &frecuencia, &pf);
      if (!okPZEM) {
        voltaje = corriente = potencia = energia = frecuencia = pf = -1;
      }

      // CSV line
      String dataString;
      dataString.reserve(200);

      dataString += String(tiempo_s);        dataString += ",";
      dataString += String(busVoltage1, 4);  dataString += ",";
      dataString += String(shuntVoltage1, 6);dataString += ",";
      dataString += String(current1_A, 6);   dataString += ",";
      dataString += String(power1_W, 4);     dataString += ",";

      dataString += String(busVoltage2, 4);  dataString += ",";
      dataString += String(shuntVoltage2, 6);dataString += ",";
      dataString += String(current2_A, 6);   dataString += ",";
      dataString += String(power2_W, 4);     dataString += ",";

      dataString += String(voltaje,    3);   dataString += ",";
      dataString += String(corriente,  3);   dataString += ",";
      dataString += String(potencia,   3);   dataString += ",";
      dataString += String(energia,    3);   dataString += ",";
      dataString += String(frecuencia, 2);   dataString += ",";
      dataString += String(pf,         3);

      // SD write
      logFile.println(dataString);

      // Mostrar en consola (Serial + web)
      Serial.println(dataString);

      if (millis() - lastWebPushMs >= PERIODO_WEB_MS) {
        lastWebPushMs = millis();

      float ipz = okPZEM ? corriente : -1;
      float ppz = okPZEM ? potencia  : -1;

      // Formato: t_s,vbus1,i1,p1,vbus2,i2,p2,ipz,ppz para la WEB (más ligero)
      ultimaLineaCSV  = String(tiempo_s);
      ultimaLineaCSV += "," + String(busVoltage1, 3);
      ultimaLineaCSV += "," + String(current1_A, 3);
      ultimaLineaCSV += "," + String(power1_W,  2);
      ultimaLineaCSV += "," + String(busVoltage2, 3);
      ultimaLineaCSV += "," + String(current2_A, 3);
      ultimaLineaCSV += "," + String(power2_W,  2);
      ultimaLineaCSV += "," + String(ipz,       3);
      ultimaLineaCSV += "," + String(ppz,       2);
      }
    }
  }
}

