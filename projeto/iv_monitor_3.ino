#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

// Configuração da rede
constexpr const char* WIFI_SSID = "arduino"; // É melhor usar um hotspot no celular, por causa do captive portal da universidade
constexpr const char* WIFI_PASSWD = "arduino123";
constexpr const char* SERVER_URL = "http://192.168.3.126:5000/send"; // A porta e o /send precisam continuar, só mudar o IP

// Definição dos pinos
#define LDR_PIN A0
#define BUTTON_PIN D8 // Liga e desliga a calibragem
#define GREEN_LED_PIN D6 // Pisca quando cai a gota
#define YELLOW_LED_PIN D2 // Mantém aceso enquanto ta calibrando

// Constantes
constexpr unsigned int DEBOUNCE_INTERVAL_MS = 2000;
constexpr unsigned int SEND_INTERVAL_MS = 2000;
constexpr unsigned int GREEN_LED_BLINK_TIME_MS = 100;
constexpr float ALPHA = 0.2; // Usado na função updateBaseline para calcular a média móvel exponencial (esse valor não precisa mudar, só se usar um ldr e laser diferente)
constexpr unsigned int OVERSAMPLE_SIZE = 64; // Usado na função readSensorAmplified para calcular o oversample
constexpr float threshold_multiplier = 0.3;

// Variáveis globais
long light_baseline = 0; // Valor de s_t do EMA
long calibrated_threshold = 0; // Threshold para detectar a gota
bool is_calibrating = false;
bool is_inside_drop = false;

unsigned long int button_debounce_timer = 0; // Estado do botão
long calibration_max_noise = 0; // Variável para armazenar o ruído máximo e calibrar o threshold

unsigned long int green_led_timer = 0; // Temporizador do led verde
bool drop_reported = false; // Debounce para não enviar a mesma gota várias vezes pro servidor

unsigned int counter = 0;

void setup() {
  Serial.begin(115200);
  
  pinMode(BUTTON_PIN, INPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);

  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);

  WiFi.mode(WIFI_STA); // Define o modo de wifi como station mode (o ESP é um client)
  WiFi.begin(WIFI_SSID, WIFI_PASSWD); // Inicia a conexão (Em teoria o ESP tenta reinicar se a conexão falhar)

  Serial.print("Conectando ao WiFi");

  Serial.println();
  Serial.print("Conectado! IP: ");
  Serial.println(WiFi.localIP());

  light_baseline = readSensorAmplified(); // Começa com um read logo no início, senão o EMA buga tudo

  // Autocalibragem no início
  is_calibrating = true;
  digitalWrite(YELLOW_LED_PIN, HIGH);

  unsigned long start_calib = millis();

  while(millis() - start_calib < 3000){
    detectDrop();
    delay(10); // delay pra não travar o watchdog (tem como desativar, mas eu não consegui)
  }

  is_calibrating = false;
  digitalWrite(YELLOW_LED_PIN, LOW);

  handleCalibration();
}

void loop() {
  bool drop_detected = detectDrop();
  handleButtonPress();

  digitalWrite(YELLOW_LED_PIN, is_calibrating);
  blinkGreenLEDat(drop_detected);

  if(drop_detected) counter++;
  Serial.println(counter);
  sendDropTime(drop_detected);
}

void blinkGreenLEDat(bool should_blink){
  static bool last_blink = false;
  // Pisca o led por GREEN_LED_BLINK_TIME_MS milissegundos
  if(should_blink && !last_blink){
    digitalWrite(GREEN_LED_PIN, HIGH);
    green_led_timer = millis();
  }
  last_blink = should_blink;
  if(millis() - green_led_timer > GREEN_LED_BLINK_TIME_MS){
    digitalWrite(GREEN_LED_PIN, LOW);
  }
}

long readSensorAmplified(void){
  /*
    Usa um oversample invés dos 1024 valores do sensor.
    Com o oversample_size em 64, ele vai ler 64 sensores e guardar a soma, então o range "é de 0 até SIZE * 1024" (na verdade, não é, mas é como se fosse).
    A variação quando a gota de água passa pelo sensor, dependendo da luz, pode ser pequena.
    Mesmo que esse método demore oversample_size * 10 microssegundos, ele é melhor do que ler direto do sensor
  */
  long sum = 0;
  for(int i = 0; i < OVERSAMPLE_SIZE; i++){
    sum += analogRead(LDR_PIN);
    delayMicroseconds(10);
  }
  return sum;
}

bool detectDrop(void){
  long raw_value = readSensorAmplified();

  Serial.print("Base:"); Serial.print(light_baseline);
  Serial.print(" Raw:"); Serial.print(raw_value);
  Serial.print(" Trigger:"); Serial.println(light_baseline - calibrated_threshold);

  // Se tiver calibrando, só coleta os dados do ruído
  if(is_calibrating){
    long noise = abs(raw_value - light_baseline);
    if(noise > calibration_max_noise){
      calibration_max_noise = noise;
    }
    // Continua atualizando a baseline pra normalizar
    updateBaseline(raw_value);
    return false;
  }
  // Calcula a diferença entre a média base e o valor atual
  long diff = (light_baseline - raw_value);

  if(!is_inside_drop){
    if(diff > calibrated_threshold){
      is_inside_drop = true;
      return true;
    }
    else{
      updateBaseline(raw_value);
      return false;
    }
  }
  else{
    if(diff < (calibrated_threshold * threshold_multiplier)){
      is_inside_drop = false;
    }
    return false;
  }
}

void updateBaseline(long raw_value){
  // Média móvel exponencial para normalizar o ruido de luz no LDR
  light_baseline = (long)((ALPHA * raw_value) + ((1.0 - ALPHA) * light_baseline));
}

void handleCalibration(void){
  if(is_calibrating){
    // Reseta a variável de ruido e a baseline
    calibration_max_noise = 0;
    light_baseline = readSensorAmplified();
  }
  else{
    // Define o novo threshold (não é o threshold do sensor, é da soma de oversample_size sensores, por isso é sempre alto)
    calibrated_threshold = (long)(calibration_max_noise * 1.5); // limiar = maior ruido encontrado * 1.4 (o 1.4 é só uma margem)
    if(calibrated_threshold < 100) calibrated_threshold = 100; // Se for menor que 100 a margem some, então é só para garantir que não vai ficar em 0
  }
}

void handleButtonPress(void){
  // Usei só a variável de tempo como debounce, já que o ideal é calibrar por no mínimo alguns segundos
  if(millis() - button_debounce_timer < DEBOUNCE_INTERVAL_MS) return;

  if(digitalRead(BUTTON_PIN) == HIGH){
    button_debounce_timer = millis();
    is_calibrating = !is_calibrating;
    handleCalibration();
  }
  // Se esquecer de desligar o modo de calibragem ele desliga sozinho, aproveitando a variável que era pra ser do botão
  if(is_calibrating && (millis() - button_debounce_timer > 5 * DEBOUNCE_INTERVAL_MS)){
    is_calibrating = false;
    handleCalibration();
  }
}

bool sendPayload(String payload){
  WiFiClient client;
  HTTPClient http;

  Serial.print("[HTTP] Enviando payload: ");
  Serial.println(payload);

  if(!http.begin(client, SERVER_URL)){
    Serial.println("[HTTP] Falha no begin()");
    return false;
  }

  http.addHeader("Content-Type", "text/plain");

  int httpCode = http.POST(payload);

  if(httpCode <= 0){
    Serial.print("[HTTP] Erro POST: ");
    Serial.println(http.errorToString(httpCode));
    http.end();
    return false;
  }

  String response = http.getString();

  Serial.print("[HTTP] Código: ");
  Serial.println(httpCode);
  Serial.print("[HTTP] Resposta: ");
  Serial.println(response);

  http.end();

  return (httpCode == 200);
}

bool sendDropTime(bool drop_detected){
  // A única coisa que o código envia pro server é o tempo que caiu a gota, o resto é melhor calcular no pc, senão vai estourar a RAM do ESP
  if(WiFi.status() != WL_CONNECTED){
    return false;
  }

  if(drop_detected && !is_calibrating){
    if(!drop_reported){
      sendPayload("1");
    }
    drop_reported = true;
  }
  else if(!drop_detected){
    drop_reported = false;
  }
  return true;
}