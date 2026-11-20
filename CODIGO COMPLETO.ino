#include <Arduino.h>
#include <math.h>
#include <esp_arduino_version.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_MLX90614.h>
#include <PulseSensorPlayground.h>

// ================= CONEXIONES =================
#define TFT_CS     5
#define TFT_DC     2
#define TFT_RST    4
#define TFT_SCLK  18
#define TFT_MOSI  23

const int PULSE_PIN = 34;
const int BUZZER_PIN = 25;
const int I2C_SDA_PIN = 21;
const int I2C_SCL_PIN = 22;

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
Adafruit_MLX90614 sensorTemperatura;
PulseSensorPlayground pulseSensor;

// El GY-906 / MLX90614 mide por infrarrojo la temperatura del objeto.
bool sensorTemperaturaDisponible = false;
float temperaturaObjetoC = 0.0f;
unsigned long ultimaLecturaTemperatura = 0;
const unsigned long PERIODO_TEMPERATURA = 1000;

// ================= BUZZER Y CORAZON =================
// false: piezo pasivo; true: modulo activo, disparado en HIGH.
const bool BUZZER_ACTIVO = false;
const int FRECUENCIA_BUZZER = 2000;
const int CANAL_BUZZER = 6;  // Para Arduino-ESP32 2.x.
const unsigned long DURACION_PITIDO = 60;
const unsigned long DURACION_CORAZON = 140;

bool buzzerSonando = false;
bool corazonEncendido = false;
unsigned long inicioAviso = 0;

// ================= MUESTREO Y FILTRO DE PULSO =================
// ADC de 10 bits. Al encender, dejar el sensor libre hasta "Coloque el dedo".
const uint32_t PERIODO_MUESTRA = 5;       // filtro a 200 Hz nominales
const unsigned long PERIODO_GRAFICA = 20;
const unsigned long PERIODO_TEXTO = 250;
const uint32_t PERIODO_BPM_TFT = 2000;     // tiempo minimo entre estimaciones visibles
const int INTERVALOS_BPM_TFT = 6;
const unsigned long PERIODO_SERIAL = 20;
const uint32_t ARRANQUE_FILTRO_MS = 250;
const uint32_t TRAMO_RUIDO_MS = 500;      // calibracion total de 1,75 s
const uint32_t IBI_MIN_MS = 300;
const uint32_t IBI_MAX_MS = 1800;
const uint32_t SIN_PULSO_MS = 4000;       // desde el ultimo latido aceptado
const float AMPLITUD_MIN_PPG = 10.0f;    // pico a valle, en cuentas filtradas
const float FACTOR_RUIDO = 1.65f;
const int AMPLITUD_MAX_PPG = 600;

// ================= GRAFICA =================
const int GRAFICA_ARRIBA = 100;
const int GRAFICA_ABAJO = 219;
const int GRAFICA_CENTRO = 160;
const int PASO_CUADRICULA = 20;

const uint16_t COLOR_FONDO = ST77XX_BLACK;
const uint16_t COLOR_CUADRICULA = 0x2104;
const uint16_t COLOR_CENTRO = 0x4208;

// ================= VARIABLES =================
int bpm = 0;  // frecuencia estimada a partir de los intervalos recientes
int bpmMostrado = -1;
bool hayIntervaloBpmNuevo = false;
int temperaturaAnteriorDecimas = -9999;
int estadoAnterior = -1;
bool filtroIniciado = false;
int muestraPrevia1 = 0, muestraPrevia2 = 0, ultimaLimpia = 0;
float suave1 = 0, suave2 = 0;
float lineaBase = 0;
float curva = 0;
float ruidoRapido = 0;
uint32_t ultimaMuestraMs = 0, ultimoReinicioFiltro = 0;

bool calibrado = false;
uint8_t tramoRuido = 0;
uint32_t inicioTramo = 0;
int ruidoPorTramo[3] = {};
float minimoTramo = 0, maximoTramo = 0;
float amplitudMinima = AMPLITUD_MIN_PPG;

bool siguiendoPico = false, hayPicoAnterior = false;
float valle = 0, pico = 0;
uint32_t inicioSubida = 0, instantePico = 0, picoAnteriorMs = 0;
uint32_t ultimoLatidoMs = 0;
int intervaloAnterior = 0, periodoPulsoMs = 0;
float escalaVista = 20.0f;
int xGrafica = 0;
int yAnterior = GRAFICA_CENTRO;
unsigned long ultimaGrafica = 0;
unsigned long ultimoTexto = 0;
unsigned long ultimoSerial = 0;

// ================= SONIDO SIN PAUSAS =================
bool configurarBuzzer() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  if (BUZZER_ACTIVO) return true;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (!ledcAttach(BUZZER_PIN, FRECUENCIA_BUZZER, 8)) {
    return false;
  }
  ledcWrite(BUZZER_PIN, 0);
#else
  if (ledcSetup(CANAL_BUZZER, FRECUENCIA_BUZZER, 8) == 0) {
    return false;
  }
  ledcAttachPin(BUZZER_PIN, CANAL_BUZZER);
  ledcWrite(CANAL_BUZZER, 0);
#endif
  return true;
}

void activarSonido(bool activar) {
  if (BUZZER_ACTIVO) {
    digitalWrite(BUZZER_PIN, activar ? HIGH : LOW);
    return;
  }

  // PWM por hardware: no usa tone() ni esperas.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(BUZZER_PIN, activar ? 128 : 0);
#else
  ledcWrite(CANAL_BUZZER, activar ? 128 : 0);
#endif
}

void dibujarCorazon(bool encendido) {
  // Corazon junto a la etiqueta BPM; no ocupa la zona de los numeros.
  const int cx = tft.width() / 2 - 18;
  const int cy = 14;
  tft.fillRect(cx - 12, 2, 24, 24, COLOR_FONDO);
  int r = encendido ? 5 : 4;
  uint16_t color = encendido ? ST77XX_RED : 0x6000;

  tft.fillCircle(cx - r, cy - r, r, color);
  tft.fillCircle(cx + r, cy - r, r, color);
  tft.fillTriangle(cx - 2 * r, cy - r,
                   cx + 2 * r, cy - r,
                   cx, cy + 2 * r, color);
}

void avisarLatido(unsigned long ahora) {
  inicioAviso = ahora;
  activarSonido(true);
  buzzerSonando = true;
  dibujarCorazon(true);
  corazonEncendido = true;
}

void actualizarAvisos(unsigned long ahora) {
  if (buzzerSonando &&
      (bpm == 0 || ahora - inicioAviso >= DURACION_PITIDO)) {
    activarSonido(false);
    buzzerSonando = false;
  }
  if (corazonEncendido &&
      (bpm == 0 || ahora - inicioAviso >= DURACION_CORAZON)) {
    dibujarCorazon(false);
    corazonEncendido = false;
  }
}

// ================= FILTRADO Y VALIDACION DE LATIDOS =================
int medianaDeTres(int a, int b, int c) {
  return max(min(a, b), min(max(a, b), c));
}

void reiniciarPicos() {
  hayPicoAnterior = false;
  siguiendoPico = false;
  valle = 0;
  intervaloAnterior = 0;
}

// El primer tramo permite asentarse al filtro. La mediana de los tres
// tramos siguientes evita fijar el umbral por un solo golpe durante el arranque.
void medirRuidoAmbiente(uint32_t ahora) {
  minimoTramo = fminf(minimoTramo, curva);
  maximoTramo = fmaxf(maximoTramo, curva);
  uint32_t duracion = tramoRuido == 0 ? ARRANQUE_FILTRO_MS : TRAMO_RUIDO_MS;
  if (ahora - inicioTramo < duracion) return;
  if (tramoRuido > 0) {
    ruidoPorTramo[tramoRuido - 1] = lroundf(maximoTramo - minimoTramo);
  }
  inicioTramo = ahora;
  minimoTramo = maximoTramo = curva;
  if (++tramoRuido == 4) {
    int ruido = medianaDeTres(ruidoPorTramo[0], ruidoPorTramo[1], ruidoPorTramo[2]);
    amplitudMinima = fmaxf(AMPLITUD_MIN_PPG, FACTOR_RUIDO * ruido + 2.0f);
    calibrado = true;
  }
}

// Recibe solo intervalos medidos entre latidos. Un hueco por latidos perdidos
// conserva la ultima lectura hasta que hay otro intervalo medido.
void registrarIntervaloBpm(int intervaloMs, int anteriorMs) {
  static int intervalos[INTERVALOS_BPM_TFT];
  static int cantidad = 0, siguiente = 0;

  if (bpm == 0) {
    // La primera pareja ya fue confirmada por el detector. Empieza una lectura nueva.
    intervalos[0] = anteriorMs;
    cantidad = siguiente = 1;
  }
  intervalos[siguiente] = intervaloMs;
  siguiente = (siguiente + 1) % INTERVALOS_BPM_TFT;
  if (cantidad < INTERVALOS_BPM_TFT) cantidad++;

  int sumaMs = 0, menor = intervalos[0], mayor = intervalos[0];
  for (int i = 0; i < cantidad; i++) {
    sumaMs += intervalos[i];
    menor = min(menor, intervalos[i]);
    mayor = max(mayor, intervalos[i]);
  }
  int usados = cantidad;
  if (cantidad >= 3) {
    // Promediar sin los dos extremos reduce el efecto de un latido adelantado o tardio.
    sumaMs -= menor + mayor;
    usados -= 2;
  }
  bpm = (60000 * usados + sumaMs / 2) / sumaMs;
  hayIntervaloBpmNuevo = true;
}

void registrarPico(uint32_t tiempo, float amplitud, uint32_t ahora) {
  uint32_t separacion = tiempo - picoAnteriorMs;
  if (hayPicoAnterior && separacion < IBI_MIN_MS) {
    // Medir tambien los picos demasiado rapidos evita contar uno de cada dos.
    picoAnteriorMs = tiempo;
    intervaloAnterior = 0;
    return;
  }
  int intervalo = hayPicoAnterior && separacion <= IBI_MAX_MS ? (int)separacion : 0;
  int anterior = intervaloAnterior;
  picoAnteriorMs = tiempo;
  hayPicoAnterior = true;
  intervaloAnterior = intervalo;

  bool enSeguimiento = bpm > 0;
  int desdeLatido = enSeguimiento ? (int)(tiempo - ultimoLatidoMs) : 0;
  bool alRitmo = enSeguimiento && desdeLatido >= (int)IBI_MIN_MS &&
      desdeLatido <= (int)IBI_MAX_MS &&
      abs(desdeLatido - periodoPulsoMs) <= periodoPulsoMs / 3;
  bool parRegular = intervalo > 0 && anterior > 0 &&
      abs(intervalo - anterior) <= min(intervalo, anterior) / 4;

  if (alRitmo) {
    periodoPulsoMs = (2 * periodoPulsoMs + desdeLatido + 1) / 3;
  } else if (parRegular) {
    // Tres picos permiten iniciar la lectura o reconocer un cambio de ritmo.
    periodoPulsoMs = (intervalo + anterior + 1) / 2;
  } else {
    if (!enSeguimiento) return;
    // Tras uno o dos latidos perdidos, conservar las BPM y avisar solo del pico real.
    int ciclos = (desdeLatido + periodoPulsoMs / 2) / periodoPulsoMs;
    if (ciclos < 2 || ciclos > 3 ||
        abs(desdeLatido - ciclos * periodoPulsoMs) > periodoPulsoMs / 5) return;
  }
  float referencia = 0.70f * amplitud;
  escalaVista = enSeguimiento ? 0.7f * escalaVista + 0.3f * referencia : referencia;
  ultimoLatidoMs = tiempo;
  if (alRitmo || parRegular) {
    registrarIntervaloBpm(alRitmo ? desdeLatido : intervalo, anterior);
  }
  avisarLatido(ahora);
}

// Un pico debe partir de un valle, superar el ruido y volver a bajar
// antes de contarse. El instante utilizado para las BPM es el del maximo.
void detectarPico(uint32_t ahora) {
  // El ruido rapido puede aumentar despues de la calibracion, por ejemplo
  // al cambiar la iluminacion. Exigir que el pulso destaque sobre ese ruido.
  float minimoPulso = fmaxf(amplitudMinima, 2.7f * ruidoRapido);
  if (bpm > 0) {
    // Admitir ondas debiles sin bajar mucho del ruido ambiente medido al encender.
    minimoPulso = fmaxf(0.75f * minimoPulso, amplitudMinima - 5.0f);
  }
  if (!siguiendoPico) {
    valle = fminf(valle, curva);
    if (curva >= 0.40f * minimoPulso && curva - valle >= 0.45f * minimoPulso) {
      siguiendoPico = true;
      pico = curva;
      inicioSubida = instantePico = ahora;
    }
    return;
  }
  if (curva > pico) {
    pico = curva;
    instantePico = ahora;
  }
  float amplitud = pico - valle;
  float caida = fmaxf(1.5f, 0.14f * amplitud);
  if (curva > pico - caida && ahora - inicioSubida <= 750) return;

  siguiendoPico = false;
  valle = curva;
  uint32_t anchoMs = ahora - inicioSubida;
  if (anchoMs >= 35 && anchoMs <= 750 &&
      amplitud >= minimoPulso && amplitud <= AMPLITUD_MAX_PPG) {
    registrarPico(instantePico, amplitud, ahora);
  }
}

void filtrarMuestra(int muestra, uint32_t ahora, uint32_t dt) {
  if (!filtroIniciado || dt > 100) {
    muestraPrevia1 = muestraPrevia2 = ultimaLimpia = muestra;
  }
  // Diferencia respecto a los dos vecinos: mide cambios muy rapidos sin
  // penalizar tanto la subida continua de un pulso de mayor frecuencia.
  float residuoRapido = fabsf(muestraPrevia1 - 0.5f * (muestraPrevia2 + muestra));
  int limpia = medianaDeTres(muestraPrevia2, muestraPrevia1, muestra);
  muestraPrevia2 = muestraPrevia1;
  muestraPrevia1 = muestra;
  bool salto = abs(limpia - ultimaLimpia) > 140;
  ultimaLimpia = limpia;
  if (!filtroIniciado || dt > 100 || limpia <= 5 || limpia >= 1018 || salto) {
    suave1 = suave2 = lineaBase = limpia;
    curva = 0;
    ruidoRapido = 0;
    ultimoReinicioFiltro = ahora;
    filtroIniciado = true;
    // Un salto aislado invalida este pico. La lectura vence por tiempo en loop().
    reiniciarPicos();
  } else {
    // Dos etapas pasabajos y una referencia lenta para retirar la continua.
    // Usar el tiempo real entre muestras tambien cuando la TFT tarda en dibujar.
    float a = (float)dt / (24.0f + dt);
    suave1 += a * (limpia - suave1);
    suave2 += a * (suave1 - suave2);
    ruidoRapido += ((float)dt / (320.0f + dt)) * (residuoRapido - ruidoRapido);
    lineaBase += ((float)dt / (400.0f + dt)) * (suave2 - lineaBase);
    curva = suave2 - lineaBase;
  }
  if (!calibrado) {
    medirRuidoAmbiente(ahora);
  } else if (ahora - ultimoReinicioFiltro >= 220) {
    detectarPico(ahora);
  }
}

// ================= CUADRICULA =================
void restaurarColumna(int x) {
  int alto = GRAFICA_ABAJO - GRAFICA_ARRIBA + 1;
  uint16_t color = (x % PASO_CUADRICULA == 0)
      ? COLOR_CUADRICULA : COLOR_FONDO;
  tft.drawFastVLine(x, GRAFICA_ARRIBA, alto, color);
  for (int y = GRAFICA_ARRIBA; y <= GRAFICA_ABAJO;
       y += PASO_CUADRICULA) {
    tft.drawPixel(x, y, COLOR_CUADRICULA);
  }
  tft.drawPixel(x, GRAFICA_CENTRO, COLOR_CENTRO);
}

void dibujarCuadricula() {
  const int alto = GRAFICA_ABAJO - GRAFICA_ARRIBA + 1;
  tft.fillRect(0, GRAFICA_ARRIBA, tft.width(), alto, COLOR_FONDO);
  for (int x = 0; x < tft.width(); x += PASO_CUADRICULA) {
    tft.drawFastVLine(x, GRAFICA_ARRIBA, alto, COLOR_CUADRICULA);
  }
  for (int y = GRAFICA_ARRIBA; y <= GRAFICA_ABAJO; y += PASO_CUADRICULA) {
    tft.drawFastHLine(0, y, tft.width(), COLOR_CUADRICULA);
  }
  tft.drawFastHLine(0, GRAFICA_CENTRO, tft.width(), COLOR_CENTRO);
}

// ================= INTERFAZ TFT =================
void dibujarNumeroGrande(const char* texto, int x, int ancho,
                         int tamanioMaximo, uint16_t color) {
  const int y = 28;
  const int alto = 50;
  tft.fillRect(x, y, ancho, alto, COLOR_FONDO);

  // Mantiene los valores dentro de su mitad, incluso con 3 digitos de BPM
  // o temperaturas negativas/de 3 digitos, sin invadir el otro valor.
  int tamanio = tamanioMaximo;
  int16_t x1, y1;
  uint16_t anchoTexto, altoTexto;
  do {
    tft.setTextSize(tamanio);
    tft.getTextBounds(texto, 0, 0, &x1, &y1, &anchoTexto, &altoTexto);
    if (anchoTexto <= ancho - 8 && altoTexto <= alto) break;
    tamanio--;
  } while (tamanio > 0);

  tft.setTextColor(color);
  tft.setCursor(x + (ancho - (int)anchoTexto) / 2 - x1,
                y + (alto - (int)altoTexto) / 2 - y1);
  tft.print(texto);
}

void dibujarInterfaz() {
  tft.fillScreen(COLOR_FONDO);
  tft.setTextWrap(false);
  const int division = tft.width() / 2;
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(8, 7);
  tft.print("BPM");
  tft.setCursor(division + 8, 7);
  tft.print("TEMP");
  tft.drawCircle(division + 70, 10, 2, ST77XX_CYAN);
  tft.setCursor(division + 76, 7);
  tft.print("C");
  tft.drawFastVLine(division, 4, 74, COLOR_CENTRO);
  tft.drawFastHLine(0, 94, tft.width(), ST77XX_WHITE);
  dibujarCuadricula();
  dibujarCorazon(false);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(5, 229);
  tft.print("Dedo quieto - presion suave");
}

void actualizarTexto() {
  static uint32_t ultimaActualizacionBpm = 0;
  uint32_t ahora = millis();
  int estado = !calibrado ? 0 : (bpm > 0 ? 3 : (hayPicoAnterior ? 2 : 1));
  if (bpm > 0 && ahora - ultimoLatidoMs > (uint32_t)(periodoPulsoMs * 7 / 4)) {
    estado = 4;
    hayIntervaloBpmNuevo = false;
  }
  const int division = tft.width() / 2;

  int visible = bpm;
  if (bpm > 0 && bpmMostrado > 0) {
    visible = bpmMostrado;
    if (estado == 3 && hayIntervaloBpmNuevo &&
        ahora - ultimaActualizacionBpm >= PERIODO_BPM_TFT) {
      // Los cambios sostenidos avanzan por pasos, mayores si el ritmo varia mucho.
      int diferencia = bpm - bpmMostrado;
      int paso = min(8, max(3, abs(diferencia) / 3));
      visible += max(-paso, min(paso, diferencia));
    }
  }
  if (visible != bpmMostrado) {
    char textoBpm[16];
    snprintf(textoBpm, sizeof(textoBpm), "%d", visible);
    dibujarNumeroGrande(textoBpm, 0, division, 6, ST77XX_WHITE);
    bpmMostrado = visible;
    ultimaActualizacionBpm = ahora;
    hayIntervaloBpmNuevo = false;
  }

  int temperaturaDecimas = sensorTemperaturaDisponible
      ? (int)lroundf(temperaturaObjetoC * 10.0f) : -9998;
  if (temperaturaDecimas != temperaturaAnteriorDecimas) {
    char textoTemperatura[16];
    if (sensorTemperaturaDisponible) {
      snprintf(textoTemperatura, sizeof(textoTemperatura), "%.1f",
                (double)temperaturaObjetoC);
    } else {
      snprintf(textoTemperatura, sizeof(textoTemperatura), "Error");
    }
    dibujarNumeroGrande(textoTemperatura, division + 1,
                         tft.width() - division - 1, 5,
                         sensorTemperaturaDisponible
                             ? ST77XX_WHITE : ST77XX_YELLOW);
    temperaturaAnteriorDecimas = temperaturaDecimas;
  }

  if (estado != estadoAnterior) {
    tft.fillRect(0, 78, tft.width(), 12, COLOR_FONDO);
    tft.setTextSize(1);
    tft.setCursor(5, 80);
    if (estado == 0) {
      tft.setTextColor(ST77XX_YELLOW);
      tft.print("Calibrando - deje el sensor libre");
    } else if (estado == 1) {
      tft.setTextColor(ST77XX_WHITE);
      tft.print("Coloque el dedo");
    } else if (estado == 2) {
      tft.setTextColor(ST77XX_YELLOW);
      tft.print("Comprobando pulso");
    } else if (estado == 3) {
      tft.setTextColor(ST77XX_GREEN);
      tft.print("Pulso detectado");
    } else {
      tft.setTextColor(ST77XX_YELLOW);
      tft.print("Ultima lectura - buscando pulso");
    }
    estadoAnterior = estado;
  }
}

void dibujarCurva(float valor) {
  static bool habiaTrazo = false;
  if (bpm == 0) {
    if (habiaTrazo) dibujarCuadricula();
    habiaTrazo = false;
    xGrafica = 0;
    yAnterior = GRAFICA_CENTRO;
    return;
  }
  habiaTrazo = true;
  // Solo en la TFT: escala por amplitud y compresion suave de picos.
  // El cero siempre cae en la linea central y quedan 8 pixeles de margen.
  const int radio = min(GRAFICA_CENTRO - GRAFICA_ARRIBA,
                        GRAFICA_ABAJO - GRAFICA_CENTRO) - 8;
  // La parte central es lineal. Los picos grandes se acercan suavemente
  // al margen; no se cortan contra el borde al cambiar la amplitud.
  float desplazamiento = 0.70f * radio * valor / escalaVista;
  const float zonaLineal = 0.75f * radio;
  float exceso = fabsf(desplazamiento) - zonaLineal;
  if (exceso > 0.0f) {
    float margen = radio - zonaLineal;
    desplazamiento = copysignf(zonaLineal + margen * exceso / (margen + exceso), valor);
  }
  int y = GRAFICA_CENTRO - (int)lroundf(desplazamiento);
  restaurarColumna(xGrafica);
  if (xGrafica > 0) {
    tft.drawLine(xGrafica - 1, yAnterior, xGrafica, y, ST77XX_GREEN);
  } else {
    tft.drawPixel(xGrafica, y, ST77XX_GREEN);
  }
  yAnterior = y;
  xGrafica++;
  if (xGrafica >= tft.width()) xGrafica = 0;
}

void detenerConError(const char* linea1, const char* linea2) {
  tft.fillScreen(COLOR_FONDO);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_RED);
  tft.setCursor(5, 60);
  tft.print(linea1);
  tft.setCursor(5, 90);
  tft.print(linea2);
  Serial.println(linea1);
  Serial.println(linea2);
  while (true) delay(1000);
}

// ================= SERIAL PLOTTER =================
void imprimirDiagnostico() {
  Serial.print("PPG:");
  Serial.println(bpm > 0 ? curva : 0.0f, 2);
}

void leerTemperatura(unsigned long ahora) {
  if (ahora - ultimaLecturaTemperatura < PERIODO_TEMPERATURA) return;
  ultimaLecturaTemperatura = ahora;

  if (!sensorTemperaturaDisponible) return;

  float nuevaTemperatura = sensorTemperatura.readObjectTempC();
  if (isfinite(nuevaTemperatura)) {
    temperaturaObjetoC = nuevaTemperatura;
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  // I2C predeterminado del ESP32: SDA=21, SCL=22.
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  sensorTemperaturaDisponible = sensorTemperatura.begin(0x5A, &Wire);
  if (!sensorTemperaturaDisponible) {
    Serial.println("No se detecta el GY-906 / MLX90614. Verifique SDA/SCL y alimentacion.");
  } else {
    float lecturaInicial = sensorTemperatura.readObjectTempC();
    if (isfinite(lecturaInicial)) temperaturaObjetoC = lecturaInicial;
  }

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(240, 280);
  tft.setRotation(1);
  dibujarInterfaz();
  actualizarTexto();

  if (!configurarBuzzer()) {
    detenerConError("Error en buzzer", "Revise PWM / pin");
  }

  analogReadResolution(10);
  pinMode(PULSE_PIN, INPUT);
  pulseSensor.analogInput(PULSE_PIN);
  if (!pulseSensor.begin()) {
    detenerConError("Error al iniciar", "Pulse Sensor");
  }
  if (!pulseSensor.UsingHardwareTimer) {
    detenerConError("Sin temporizador", "Revise biblioteca");
  }
  inicioTramo = ultimaMuestraMs = millis();
}

// ================= LOOP =================
void loop() {
  pulseSensor.sawNewSample();
  uint32_t ahora = millis();
  if ((bpm > 0 && ahora - ultimoLatidoMs >= SIN_PULSO_MS) ||
      (bpm == 0 && hayPicoAnterior && ahora - picoAnteriorMs > IBI_MAX_MS + 200)) {
    bpm = 0;
    periodoPulsoMs = 0;
    reiniciarPicos();
  }
  uint32_t dt = ahora - ultimaMuestraMs;
  if (dt >= PERIODO_MUESTRA) {
    ultimaMuestraMs = ahora;
    filtrarMuestra(pulseSensor.getLatestSample(), ahora, dt);
  }

  if (ahora - ultimaGrafica >= PERIODO_GRAFICA) {
    ultimaGrafica = ahora;
    dibujarCurva(curva);
  }

  leerTemperatura(ahora);

  actualizarAvisos(ahora);

  if (ahora - ultimoTexto >= PERIODO_TEXTO) {
    ultimoTexto = ahora;
    actualizarTexto();
  }
  if (ahora - ultimoSerial >= PERIODO_SERIAL) {
    ultimoSerial = ahora;
    imprimirDiagnostico();
  }
}
