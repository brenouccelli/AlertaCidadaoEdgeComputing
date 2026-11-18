/*
  ============================================================================
  PROJETO   : Alerta Cidadao - Monitoramento e Prevencao de Enchentes (CitizenTech)
  DISCIPLINA: Edge Computing & Computer Systems - Global Solution 2026.2
  INSTITUICAO: FIAP
  INTEGRANTES:
    - Murillo Kirsten Minamoto - RM 576741
    - Heitor Zanelato - RM 576542
    - Breno Uccelli - RM 576542
    - João Pedro Castanheira Perez - RM 576203
  ----------------------------------------------------------------------------
  Placa: Arduino Uno | Prototipo academico: os limites NAO representam limites
  reais de seguranca contra enchentes.

  Ciclo Edge: MEDIR -> DECIDIR (validar + suavizar + comparar) -> ALERTAR/AGIR
  Tudo roda no Arduino, sem internet, nuvem ou servidor externo.

  Principio do HC-SR04: mede a DISTANCIA ate a superficie da agua.
    Distancia MENOR = agua mais PROXIMA do sensor = nivel MAIOR = mais risco.
    nivel = ALTURA_DO_SENSOR - distancia

  O DHT22 e um sensor COMPLEMENTAR (temperatura/umidade do ar): so informa no
  LCD e no Serial. Ele NUNCA substitui nem altera a medicao do nivel da agua.

  Configuracoes (limites e altura do sensor) ficam na EEPROM interna do
  ATmega328P e podem ser alteradas pelo Monitor Serial (digite HELP).

  Animacoes no LCD (caracteres personalizados / CGRAM, controladas por millis):
    - Inicializacao: ondas de agua se deslocando (~3,6 s).
    - Estado ALERTA: triangulo de perigo piscando + "PERIGO!" + nivel em cm.
  As animacoes NAO bloqueiam a leitura dos sensores nem o controle dos alertas.
  ============================================================================
*/

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>

// ============================ PINOS ========================================
const uint8_t PIN_BUZZER = 4;
const uint8_t PIN_LED_GREEN = 5;
const uint8_t PIN_LED_YELLOW = 6;
const uint8_t PIN_LED_RED = 7;
const uint8_t PIN_DHT = 8;
const uint8_t PIN_TRIG = 9;
const uint8_t PIN_ECHO = 10;
// LCD I2C: SDA = A4 e SCL = A5 (pinos fixos do Uno, usados pela biblioteca Wire)

// ======================= PERIFERICOS (objetos) =============================
const uint8_t LCD_ADDRESS = 0x27;
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);
DHT dht(PIN_DHT, DHT22);

// ===================== CONFIGURACAO DO SENSOR (fixa) =======================
const float DIST_MIN_VALID_CM = 2.0;           // minimo fisico do HC-SR04
const float DIST_MAX_VALID_CM = 300.0;         // alcance util definido neste projeto
const unsigned long ECHO_TIMEOUT_US = 25000UL; // ~4 m; evita bloqueio longo
const uint8_t MAX_INVALID_READINGS = 3;        // leituras invalidas seguidas -> FALHA

// Faixa fisicamente plausivel do DHT22 (fora dela = leitura invalida)
const float DHT_TEMP_MIN_C = -40.0;
const float DHT_TEMP_MAX_C = 80.0;
const uint8_t MAX_INVALID_DHT = 3;

// ============ VALORES PADRAO DA CONFIGURACAO (gravados na EEPROM) ==========
// Cenario ficticio: sensor a 200 cm acima do fundo do canal.
// Distancia < 100 cm -> nivel > 100 cm -> ATENCAO (agua subindo)
// Distancia <  50 cm -> nivel > 150 cm -> ALERTA  (perto de transbordar)
// Histerese: para DESCER de estado a agua precisa recuar 5 cm alem do limite,
// evitando "piscar" entre estados quando a leitura oscila no limite.
const float DEFAULT_ATTENTION_CM = 100.0;
const float DEFAULT_ALERT_CM = 50.0;
const float DEFAULT_HYSTERESIS_CM = 5.0;
const float DEFAULT_SENSOR_HEIGHT = 200.0;

// ======================= SUAVIZACAO (media movel) ==========================
const uint8_t MA_MAX_WINDOW = 5;
const uint8_t DIST_WINDOW = 5; // HC-SR04: 5 amostras
const uint8_t DHT_WINDOW = 3;  // DHT22: 3 amostras (le a cada 2 s)

// ======================= INTERVALOS (millis) ===============================
const unsigned long DIST_READ_INTERVAL_MS = 200; // HC-SR04
const unsigned long DHT_READ_INTERVAL_MS = 2000; // DHT22 (minimo ~2 s)
const unsigned long DISPLAY_INTERVAL_MS = 300;   // atualizacao do LCD
const unsigned long DISPLAY_PAGE_MS = 3000;      // alterna linha 2 do LCD
const unsigned long SERIAL_INTERVAL_MS = 500;    // relatorio no Serial

// Padroes sonoros / visuais (calculados com millis, sem delay)
const unsigned long ALERT_BEEP_MS = 250;        // ALERTA: liga/desliga a cada 250 ms
const unsigned long FAIL_BLINK_MS = 500;        // FALHA: pisca a cada 500 ms
const unsigned long ATTENTION_PERIOD_MS = 3000; // ATENCAO: 1 bipe curto a cada 3 s
const unsigned long ATTENTION_BEEP_MS = 100;
const uint16_t TONE_ALERT_HZ = 2000;
const uint16_t TONE_ATTENTION_HZ = 1000;
const uint16_t TONE_FAIL_HZ = 800;

// ======================= ANIMACOES DO LCD (millis) =========================
const unsigned long BOOT_DURATION_MS = 3600; // duracao total da animacao de boot
const unsigned long BOOT_TITLE_MS = 2000;    // ate aqui: titulo; depois: "Monitoramento"
const unsigned long BOOT_FRAME_MS = 100;     // velocidade das ondas
const unsigned long ALERT_FRAME_MS = 400;    // alterna os dois quadros do triangulo

// ============================ ESTADOS ======================================
enum SystemState
{
  STATE_INIT,
  STATE_NORMAL,
  STATE_ATTENTION,
  STATE_ALERT,
  STATE_FAIL
};
SystemState state = STATE_INIT;

// ===================== CONFIGURACAO EM RAM (vinda da EEPROM) ===============
struct Config
{
  float attentionCm;
  float alertCm;
  float hysteresisCm;
  float sensorHeightCm;
};
Config cfg;

// Estrutura gravada na EEPROM (endereco 0). O "magic" indica dado valido.
struct StoredData
{
  uint16_t magic;
  Config cfg;
  uint16_t boots;
  uint16_t alerts;
};
const uint16_t EEPROM_MAGIC = 0xA1C1;
const int EEPROM_ADDR = 0;

uint16_t bootCount = 0;  // quantas vezes o Arduino iniciou (persistente)
uint16_t alertCount = 0; // quantas vezes entrou em ALERTA (persistente)

// ===================== MEDIA MOVEL REUTILIZAVEL ============================
struct MovingAverage
{
  float values[MA_MAX_WINDOW];
  uint8_t window;
  uint8_t count;
  uint8_t index;
};

MovingAverage distAvg = {{0}, DIST_WINDOW, 0, 0};
MovingAverage tempAvg = {{0}, DHT_WINDOW, 0, 0};
MovingAverage humAvg = {{0}, DHT_WINDOW, 0, 0};

void maReset(MovingAverage &m)
{
  m.count = 0;
  m.index = 0;
}

void maAdd(MovingAverage &m, float v)
{
  m.values[m.index] = v;
  m.index = (m.index + 1) % m.window;
  if (m.count < m.window)
    m.count++;
}

float maValue(const MovingAverage &m)
{
  float sum = 0;
  for (uint8_t i = 0; i < m.count; i++)
    sum += m.values[i];
  return sum / m.count; // so chamar com count > 0
}

// ===================== VARIAVEIS DE PROCESSAMENTO ==========================
float rawDistanceCm = -1.0;     // ultima leitura bruta (-1 = invalida)
float smoothedDistanceCm = 0.0; // media movel da distancia
float waterLevelCm = 0.0;       // nivel estimado
uint8_t distInvalidCount = 0;

float tempC = 0.0, humidity = 0.0;
bool dhtHasData = false;
uint8_t dhtInvalidCount = 0;

unsigned long lastDistReadMs = 0, lastDhtReadMs = 0, lastDisplayMs = 0, lastSerialMs = 0;

char cmdBuf[24];
uint8_t cmdLen = 0;

// ===================== VARIAVEIS DAS ANIMACOES =============================
bool bootActive = false; // true durante a animacao de inicializacao
unsigned long bootStartMs = 0;
int16_t lastBootStep = -1;
bool bootMsgShown = false;

bool alertGlyphsLoaded = false; // true enquanto o triangulo esta na CGRAM
bool alertFrameB = false;       // quadro atual do triangulo (A=contorno, B=cheio)
unsigned long lastAlertFrameMs = 0;

// ===========================================================================
// UTILITARIOS
// ===========================================================================
const char *stateName(SystemState s)
{
  switch (s)
  {
  case STATE_INIT:
    return "INICIANDO";
  case STATE_NORMAL:
    return "NORMAL";
  case STATE_ATTENTION:
    return "ATENCAO";
  case STATE_ALERT:
    return "ALERTA";
  case STATE_FAIL:
    return "FALHA";
  }
  return "?";
}

// ===========================================================================
// CONFIGURACAO + EEPROM
// ===========================================================================
Config defaultConfig()
{
  Config c = {DEFAULT_ATTENTION_CM, DEFAULT_ALERT_CM, DEFAULT_HYSTERESIS_CM, DEFAULT_SENSOR_HEIGHT};
  return c;
}

// Valida a configuracao (a forma "!(x >= a && x <= b)" tambem rejeita NaN).
bool isConfigValid(const Config &c)
{
  if (!(c.hysteresisCm >= 1.0f && c.hysteresisCm <= 20.0f))
    return false;
  if (!(c.sensorHeightCm >= 50.0f && c.sensorHeightCm <= DIST_MAX_VALID_CM))
    return false;
  if (!(c.alertCm >= 5.0f))
    return false;
  if (!(c.attentionCm > c.alertCm + c.hysteresisCm))
    return false;
  if (!(c.attentionCm + c.hysteresisCm <= c.sensorHeightCm))
    return false;
  return true;
}

// EEPROM.put so grava os bytes que mudaram (poupa a vida util da memoria).
void saveData()
{
  StoredData d;
  d.magic = EEPROM_MAGIC;
  d.cfg = cfg;
  d.boots = bootCount;
  d.alerts = alertCount;
  EEPROM.put(EEPROM_ADDR, d);
}

void loadData()
{
  StoredData d;
  EEPROM.get(EEPROM_ADDR, d);
  if (d.magic == EEPROM_MAGIC && isConfigValid(d.cfg))
  {
    cfg = d.cfg;
    bootCount = d.boots;
    alertCount = d.alerts;
  }
  else
  { // EEPROM virgem ou dado invalido: usa os valores padrao
    Serial.println(F("[CFG] EEPROM sem dados validos. Usando padrao."));
    cfg = defaultConfig();
    bootCount = 0;
    alertCount = 0;
  }
  bootCount++;
  saveData();
}

void printConfig()
{
  Serial.print(F("[CFG] ATT="));
  Serial.print(cfg.attentionCm, 1);
  Serial.print(F(" cm | ALR="));
  Serial.print(cfg.alertCm, 1);
  Serial.print(F(" cm | HYS="));
  Serial.print(cfg.hysteresisCm, 1);
  Serial.print(F(" cm | HGT="));
  Serial.print(cfg.sensorHeightCm, 1);
  Serial.print(F(" cm | boots="));
  Serial.print(bootCount);
  Serial.print(F(" | alertas="));
  Serial.println(alertCount);
}

void printHelp()
{
  Serial.println(F("[CFG] Comandos (digite e envie):"));
  Serial.println(F("  SHOW           mostra a configuracao salva"));
  Serial.println(F("  SET ATT <cm>   distancia de ATENCAO (ex.: SET ATT 90)"));
  Serial.println(F("  SET ALR <cm>   distancia de ALERTA  (ex.: SET ALR 40)"));
  Serial.println(F("  SET HYS <cm>   histerese            (ex.: SET HYS 5)"));
  Serial.println(F("  SET HGT <cm>   altura do sensor     (ex.: SET HGT 200)"));
  Serial.println(F("  RESET          volta aos valores padrao"));
}

void executeCommand(const char *cmd)
{
  if (strcmp(cmd, "SHOW") == 0)
  {
    printConfig();
    return;
  }
  if (strcmp(cmd, "HELP") == 0)
  {
    printHelp();
    return;
  }
  if (strcmp(cmd, "RESET") == 0)
  {
    cfg = defaultConfig();
    saveData();
    Serial.println(F("[CFG] Valores padrao restaurados."));
    printConfig();
    return;
  }

  // Formato: "SET XXX valor" (XXX ocupa as posicoes 4 a 6, valor comeca na 8)
  if (strncmp(cmd, "SET ", 4) == 0 && strlen(cmd) >= 9 && cmd[7] == ' ')
  {
    float value = atof(cmd + 8);
    Config candidate = cfg;
    if (strncmp(cmd + 4, "ATT", 3) == 0)
      candidate.attentionCm = value;
    else if (strncmp(cmd + 4, "ALR", 3) == 0)
      candidate.alertCm = value;
    else if (strncmp(cmd + 4, "HYS", 3) == 0)
      candidate.hysteresisCm = value;
    else if (strncmp(cmd + 4, "HGT", 3) == 0)
      candidate.sensorHeightCm = value;
    else
    {
      Serial.println(F("[CFG] Parametro desconhecido. Digite HELP."));
      return;
    }

    if (!isConfigValid(candidate))
    {
      Serial.println(F("[CFG] Valor recusado (precisa: HYS 1-20, HGT 50-300, ALR >= 5, ATT > ALR+HYS)."));
      return;
    }
    cfg = candidate;
    saveData();
    Serial.println(F("[CFG] Salvo na EEPROM."));
    printConfig();
    return;
  }
  Serial.println(F("[CFG] Comando invalido. Digite HELP."));
}

// Le o Serial sem bloquear e monta uma linha de comando (em maiusculas).
void handleSerialCommands()
{
  while (Serial.available())
  {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r')
    {
      if (cmdLen > 0)
      {
        cmdBuf[cmdLen] = '\0';
        executeCommand(cmdBuf);
        cmdLen = 0;
      }
    }
    else if (cmdLen < sizeof(cmdBuf) - 1)
    {
      cmdBuf[cmdLen++] = (char)toupper((unsigned char)c);
    }
  }
}

// ===========================================================================
// BUZZER
// ===========================================================================
// freq = 0 silencia. So reconfigura o tone() quando a frequencia muda.
void buzzerTone(uint16_t freq)
{
  static uint16_t lastFreq = 0xFFFF;
  if (freq == lastFreq)
    return;
  lastFreq = freq;
  if (freq == 0)
    noTone(PIN_BUZZER);
  else
    tone(PIN_BUZZER, freq);
}

// ===========================================================================
// 1) LEITURA DOS SENSORES
// ===========================================================================
// HC-SR04: retorna distancia em cm ou -1.0 se invalida.
float readDistanceCm()
{
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  // pulseIn bloqueia no maximo ECHO_TIMEOUT_US (25 ms): aceitavel.
  unsigned long durationUs = pulseIn(PIN_ECHO, HIGH, ECHO_TIMEOUT_US);
  if (durationUs == 0)
    return -1.0; // sem eco (timeout)

  float cm = durationUs * 0.0343f / 2.0f; // som: 0,0343 cm/us, ida e volta
  if (cm < DIST_MIN_VALID_CM || cm > DIST_MAX_VALID_CM)
    return -1.0; // fora da faixa
  return cm;
}

// DHT22: retorna true e preenche t/h somente se a leitura for valida.
bool readDht(float &t, float &h)
{
  float newH = dht.readHumidity();
  float newT = dht.readTemperature();
  if (isnan(newT) || isnan(newH))
    return false;
  if (newT < DHT_TEMP_MIN_C || newT > DHT_TEMP_MAX_C)
    return false;
  if (newH < 0.0f || newH > 100.0f)
    return false;
  t = newT;
  h = newH;
  return true;
}

// ===========================================================================
// 2) PROCESSAMENTO E DECISAO
// ===========================================================================
// Decide o estado a partir da distancia suavizada, com histerese.
SystemState computeState(float distanceCm, SystemState current)
{
  float alertLimit = cfg.alertCm;
  float attentionLimit = cfg.attentionCm;

  if (current == STATE_ALERT)
    alertLimit += cfg.hysteresisCm;
  if (current == STATE_ALERT || current == STATE_ATTENTION)
    attentionLimit += cfg.hysteresisCm;

  if (distanceCm < alertLimit)
    return STATE_ALERT; // agua muito proxima
  if (distanceCm < attentionLimit)
    return STATE_ATTENTION; // agua subindo
  return STATE_NORMAL;      // faixa segura
}

void setState(SystemState newState)
{
  if (newState == state)
    return;
  Serial.print(F("[EVENTO] Estado: "));
  Serial.print(stateName(state));
  Serial.print(F(" -> "));
  Serial.println(stateName(newState));
  if (newState == STATE_ALERT)
  { // conta ocorrencias (grava so na mudanca de estado)
    alertCount++;
    saveData();
  }
  state = newState;
}

void processDistance(float rawCm)
{
  // --- Leitura invalida: nunca e tratada como NORMAL ---
  if (rawCm < 0)
  {
    if (distInvalidCount < 255)
      distInvalidCount++;
    if (distInvalidCount >= MAX_INVALID_READINGS)
    {
      maReset(distAvg); // descarta historico antigo
      setState(STATE_FAIL);
    }
    return; // falha isolada: mantem o estado anterior
  }

  // --- Leitura valida ---
  distInvalidCount = 0;
  maAdd(distAvg, rawCm);
  smoothedDistanceCm = maValue(distAvg);
  waterLevelCm = constrain(cfg.sensorHeightCm - smoothedDistanceCm, 0.0f, cfg.sensorHeightCm);
  setState(computeState(smoothedDistanceCm, state));
}

// DHT22 e complementar: falha dele NAO altera o estado do nivel da agua.
void processDht()
{
  float t, h;
  if (readDht(t, h))
  {
    dhtInvalidCount = 0;
    maAdd(tempAvg, t);
    maAdd(humAvg, h);
    tempC = maValue(tempAvg);
    humidity = maValue(humAvg);
    dhtHasData = true;
  }
  else
  {
    if (dhtInvalidCount < 255)
      dhtInvalidCount++;
    if (dhtInvalidCount == MAX_INVALID_DHT)
    {
      maReset(tempAvg);
      maReset(humAvg);
      dhtHasData = false;
      Serial.println(F("[AVISO] DHT22 sem leitura valida (nao afeta a decisao do nivel)."));
    }
  }
}

// ===========================================================================
// 3) ATUACAO: LEDs e buzzer (sem delay: fase calculada a partir do millis)
// ===========================================================================
void setLeds(bool green, bool yellow, bool red)
{
  digitalWrite(PIN_LED_GREEN, green);
  digitalWrite(PIN_LED_YELLOW, yellow);
  digitalWrite(PIN_LED_RED, red);
}

void updateAlerts(unsigned long now)
{
  switch (state)
  {
  case STATE_NORMAL: // verde, buzzer desligado
    setLeds(true, false, false);
    buzzerTone(0);
    break;

  case STATE_ATTENTION:
  { // amarelo + bipe curto a cada 3 s
    setLeds(false, true, false);
    bool beep = (now % ATTENTION_PERIOD_MS) < ATTENTION_BEEP_MS;
    buzzerTone(beep ? TONE_ATTENTION_HZ : 0);
    break;
  }

  case STATE_ALERT:
  { // vermelho fixo + buzzer intermitente agudo
    setLeds(false, false, true);
    bool phaseOn = ((now / ALERT_BEEP_MS) % 2) == 0;
    buzzerTone(phaseOn ? TONE_ALERT_HZ : 0);
    break;
  }

  case STATE_FAIL:
  { // amarelo + vermelho piscando + bipe grave lento
    bool phaseOn = ((now / FAIL_BLINK_MS) % 2) == 0;
    setLeds(false, phaseOn, phaseOn);
    buzzerTone(phaseOn ? TONE_FAIL_HZ : 0);
    break;
  }

  default: // STATE_INIT
    setLeds(false, false, false);
    buzzerTone(0);
    break;
  }
}

// ===========================================================================
// 4) DISPLAY LCD 16x2 - TELAS DE TEXTO
// ===========================================================================
void printLine(uint8_t row, const char *text)
{
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", text); // completa com espacos (apaga resto)
  lcd.setCursor(0, row);
  lcd.print(buf);
}

// Escreve "text" centralizado em uma linha inteira (16 colunas).
void printCentered(uint8_t row, const char *text)
{
  char buf[17];
  uint8_t len = strlen(text);
  if (len > 16)
    len = 16;
  uint8_t left = (16 - len) / 2;
  memset(buf, ' ', 16);
  memcpy(buf + left, text, len);
  buf[16] = '\0';
  printLine(row, buf);
}

// ===========================================================================
// 4a) ANIMACAO DE INICIALIZACAO: ONDAS (CGRAM)
// ===========================================================================
// Gera um caractere de onda de 5x8 pixels. A superficie da agua tem periodo
// de 5 pixels (um caractere). "phase" desloca a onda horizontalmente.
void loadWaveGlyph(uint8_t slot, uint8_t phase)
{
  // Altura da superficie (linha 0 = topo) em cada uma das 5 colunas de pixels
  static const uint8_t crest[5] = {3, 1, 2, 4, 5};
  uint8_t buf[8];
  for (uint8_t r = 0; r < 8; r++)
  {
    buf[r] = 0;
    for (uint8_t c = 0; c < 5; c++)
    {
      if (r >= crest[(c + phase) % 5])
        buf[r] |= (1 << (4 - c)); // agua abaixo da superficie
    }
  }
  lcd.createChar(slot, buf);
}

void startBootAnimation(unsigned long now)
{
  bootActive = true;
  bootStartMs = now;
  lastBootStep = -1;
  bootMsgShown = false;

  printCentered(0, "ALERTA CIDADAO");
  loadWaveGlyph(0, 0);
  loadWaveGlyph(1, 2);
  lcd.setCursor(0, 1);
  for (uint8_t c = 0; c < 16; c++)
    lcd.write((uint8_t)(c % 2)); // alterna glifos 0 e 1
}

void updateBootAnimation(unsigned long now)
{
  if (!bootActive)
    return;
  unsigned long elapsed = now - bootStartMs;

  if (elapsed >= BOOT_DURATION_MS)
  { // fim: volta para a tela principal
    bootActive = false;
    lastDisplayMs = now - DISPLAY_INTERVAL_MS; // forca redesenho imediato
    return;
  }

  if (!bootMsgShown && elapsed >= BOOT_TITLE_MS)
  {
    printCentered(0, "Monitoramento");
    bootMsgShown = true;
  }

  int16_t step = (int16_t)(elapsed / BOOT_FRAME_MS);
  if (step != lastBootStep)
  { // novo quadro: desloca as ondas
    lastBootStep = step;
    uint8_t p = (5 - (step % 5)) % 5; // fase decrescente = onda indo para a direita
    loadWaveGlyph(0, p);
    loadWaveGlyph(1, (p + 2) % 5);
  }
}

// ===========================================================================
// 4b) ANIMACAO DE ALERTA: TRIANGULO DE PERIGO (CGRAM)
// ===========================================================================
// Triangulo de advertencia de 10x16 pixels = 2x2 caracteres.
// Ordem dos 4 glifos: [0]=superior esq. [1]=superior dir. [2]=inferior esq. [3]=inferior dir.
// Quadro A: contorno com exclamacao. Quadro B: triangulo cheio com exclamacao vazada.
const uint8_t TRI_A[4][8] PROGMEM = {
    {0x01, 0x01, 0x02, 0x02, 0x04, 0x05, 0x05, 0x09},
    {0x10, 0x10, 0x08, 0x08, 0x04, 0x14, 0x14, 0x12},
    {0x09, 0x09, 0x09, 0x11, 0x10, 0x11, 0x11, 0x1F},
    {0x12, 0x12, 0x12, 0x11, 0x01, 0x11, 0x11, 0x1F}};
const uint8_t TRI_B[4][8] PROGMEM = {
    {0x01, 0x01, 0x03, 0x03, 0x07, 0x06, 0x06, 0x0E},
    {0x10, 0x10, 0x18, 0x18, 0x1C, 0x0C, 0x0C, 0x0E},
    {0x0E, 0x0E, 0x0E, 0x1E, 0x1F, 0x1E, 0x1E, 0x1F},
    {0x0E, 0x0E, 0x0E, 0x0F, 0x1F, 0x0F, 0x0F, 0x1F}};

void loadAlertGlyphs(bool frameB)
{
  uint8_t buf[8];
  for (uint8_t i = 0; i < 4; i++)
  {
    memcpy_P(buf, frameB ? TRI_B[i] : TRI_A[i], 8);
    lcd.createChar(i, buf);
  }
}

// Uma linha da tela de alerta: triangulo (2 colunas) + texto (12 colunas) + triangulo (2 colunas).
void drawAlertRow(uint8_t row, const char *text)
{
  char buf[13];
  uint8_t len = strlen(text);
  if (len > 12)
    len = 12;
  uint8_t left = (12 - len) / 2;
  memset(buf, ' ', 12);
  memcpy(buf + left, text, len);
  buf[12] = '\0';

  uint8_t g0 = row * 2;     // glifo da esquerda desta linha do triangulo
  uint8_t g1 = row * 2 + 1; // glifo da direita
  lcd.setCursor(0, row);
  lcd.write(g0);
  lcd.write(g1);
  lcd.print(buf);
  lcd.write(g0);
  lcd.write(g1);
}

void drawAlertScreen()
{
  char lvl[16];
  snprintf(lvl, sizeof(lvl), "Nivel:%3dcm", (int)(waterLevelCm + 0.5f));
  drawAlertRow(0, "PERIGO!");
  drawAlertRow(1, lvl);
}

void updateAlertAnimation(unsigned long now)
{
  if (bootActive)
    return; // a animacao de boot tem prioridade

  if (state != STATE_ALERT)
  {
    if (alertGlyphsLoaded)
    { // saiu do ALERTA: volta a tela do novo estado
      alertGlyphsLoaded = false;
      lastDisplayMs = now - DISPLAY_INTERVAL_MS;
    }
    return;
  }

  if (!alertGlyphsLoaded)
  { // acabou de entrar em ALERTA
    alertFrameB = false;
    loadAlertGlyphs(alertFrameB);
    alertGlyphsLoaded = true;
    lastAlertFrameMs = now;
    drawAlertScreen();
    return;
  }

  if (now - lastAlertFrameMs >= ALERT_FRAME_MS)
  { // alterna contorno <-> cheio
    lastAlertFrameMs = now;
    alertFrameB = !alertFrameB;
    loadAlertGlyphs(alertFrameB);
  }
}

// ===========================================================================
// 4c) TELAS PRINCIPAIS (texto)
// ===========================================================================
void updateDisplay(unsigned long now)
{
  if (bootActive)
    return; // nao interfere na animacao de boot

  char line2[20];

  switch (state)
  {
  case STATE_INIT:
    printLine(0, "Alerta Cidadao");
    printLine(1, "Iniciando...");
    return;

  case STATE_FAIL:
    printLine(0, "FALHA SENSOR");
    printLine(1, "Leitura invalida");
    return;

  case STATE_ALERT: // tela com triangulo + PERIGO! + nivel
    if (alertGlyphsLoaded)
      drawAlertScreen();
    return;

  default:
    break;
  }

  if (state == STATE_NORMAL)
    printLine(0, "ESTADO: NORMAL");
  else
    printLine(0, "ESTADO: ATENCAO");

  // Linha 2 alterna a cada 3 s: pagina 0 = agua | pagina 1 = ambiente (DHT22)
  uint8_t page = (now / DISPLAY_PAGE_MS) % 2;
  if (page == 0)
  {
    snprintf(line2, sizeof(line2), "D:%3dcm N:%3dcm",
             (int)(smoothedDistanceCm + 0.5f), (int)(waterLevelCm + 0.5f));
  }
  else if (dhtHasData)
  {
    // O AVR nao formata float no printf: usa inteiros (decimos de grau)
    int t10 = (int)(fabs(tempC) * 10.0f + 0.5f);
    snprintf(line2, sizeof(line2), "T:%c%d.%dC U:%3d%%",
             (tempC < 0 ? '-' : ' '), t10 / 10, t10 % 10, (int)(humidity + 0.5f));
  }
  else
  {
    snprintf(line2, sizeof(line2), "DHT22: sem dados");
  }
  printLine(1, line2);
}

// ===========================================================================
// 5) MONITOR SERIAL
// ===========================================================================
void printStatus()
{
  Serial.print('[');
  Serial.print(millis() / 1000.0, 1);
  Serial.print(F("s] estado="));
  Serial.print(stateName(state));

  Serial.print(F(" | bruta="));
  if (rawDistanceCm < 0)
    Serial.print(F("INVALIDA"));
  else
  {
    Serial.print(rawDistanceCm, 1);
    Serial.print(F("cm"));
  }

  Serial.print(F(" | media="));
  if (distAvg.count == 0)
    Serial.print(F("--"));
  else
  {
    Serial.print(smoothedDistanceCm, 1);
    Serial.print(F("cm"));
  }

  Serial.print(F(" | nivel="));
  if (distAvg.count == 0)
    Serial.print(F("--"));
  else
  {
    Serial.print(waterLevelCm, 1);
    Serial.print(F("cm"));
  }

  Serial.print(F(" | invalidas="));
  Serial.print(distInvalidCount);

  if (dhtHasData)
  {
    Serial.print(F(" | T="));
    Serial.print(tempC, 1);
    Serial.print(F("C U="));
    Serial.print(humidity, 0);
    Serial.println('%');
  }
  else
  {
    Serial.println(F(" | DHT22=sem dados"));
  }
}

// ===========================================================================
// SETUP E LOOP
// ===========================================================================
void setup()
{
  Serial.begin(115200);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  dht.begin();
  lcd.init();
  lcd.backlight();

  startBootAnimation(millis()); // ondas no LCD; o monitoramento ja roda no loop()

  Serial.println(F("=== Alerta Cidadao - Edge Computing (prototipo academico) ==="));
  Serial.println(F("Processamento 100% local no Arduino Uno (sem internet)."));
  loadData();
  printConfig();
  Serial.println(F("Digite HELP para ver os comandos de configuracao."));
}

void loop()
{
  unsigned long now = millis();

  handleSerialCommands();

  // MEDIR + DECIDIR: nivel da agua (HC-SR04)
  if (now - lastDistReadMs >= DIST_READ_INTERVAL_MS)
  {
    lastDistReadMs = now;
    rawDistanceCm = readDistanceCm();
    processDistance(rawDistanceCm);
  }

  // Sensor complementar (DHT22): so informativo
  if (now - lastDhtReadMs >= DHT_READ_INTERVAL_MS)
  {
    lastDhtReadMs = now;
    processDht();
  }

  // ALERTAR/AGIR: LEDs e buzzer
  updateAlerts(now);

  // Animacoes do LCD (nao bloqueantes)
  updateBootAnimation(now);
  updateAlertAnimation(now);

  // Atualizar LCD (texto)
  if (now - lastDisplayMs >= DISPLAY_INTERVAL_MS)
  {
    lastDisplayMs = now;
    updateDisplay(now);
  }

  // Relatorio serial
  if (now - lastSerialMs >= SERIAL_INTERVAL_MS)
  {
    lastSerialMs = now;
    printStatus();
  }
}