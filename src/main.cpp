#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "time.h"
#include <ESP32Servo.h>

// ===== CONFIG WIFI + MQTT =====
const char* ssid ="Edu";
const char* password = "Eduardo1902";
const char* mqtt_server = "broker.hivemq.com";

WiFiClient espClient;
PubSubClient client(espClient);

// ===== OLED 0.91" =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
TwoWire I2C_PORT = TwoWire(0);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2C_PORT, -1);

// ===== PINOS =====
#define SERVO_PIN 18
#define PIR_PIN 19
#define BUZZER_PIN 21
#define LED_VERDE 25
#define LED_VERMELHO 26
#define BUTTON_PIN 4
#define TRIG_PIN 22   // Trigger do Ultrassônico
#define ECHO_PIN 23   // Echo do Ultrassônico

Servo servoPortao;

// ===== VARIÁVEIS =====
unsigned long tempoAbertura = 0;
const unsigned long tempoFechar = 5000;
bool portaoAberto = false;

String pessoa_nome = "Joao Silva";
String carro_placa = "ABC1234";

// Ângulos configuráveis
int anguloFechado = 0;
int anguloAberto  = 120;

// ===== DISTÂNCIA DO SENSOR =====
const int distanciaLimite = 30; // cm

// ===== FUNÇÕES AUXILIARES =====
long medirDistancia() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duracao = pulseIn(ECHO_PIN, HIGH, 30000); // timeout de 30ms (~5m)
  long distancia = duracao * 0.034 / 2; // converte para cm
  return distancia;
}

String getDateTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "0000-00-00 00:00:00";
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

void publicarStatus() {
  String statusAtual = portaoAberto ? "aberto" : "fechado";
  String payload = "{";
  payload += "\"pessoa_nome\":\"" + pessoa_nome + "\",";
  payload += "\"carro_placa\":\"" + carro_placa + "\",";
  payload += "\"status\":\"" + statusAtual + "\",";
  payload += "\"data_hora\":\"" + getDateTime() + "\"";
  payload += "}";

  client.publish("portao/evento", payload.c_str());
  client.publish("portao/status", payload.c_str());
  Serial.println("Status enviado (JSON): " + payload);
}

// ===== BUZZER =====
void buzzerBeep(int freq, int tempo) {
  ledcAttachPin(BUZZER_PIN, 0);
  ledcWriteTone(0, freq);
  delay(tempo);
  ledcWriteTone(0, 0);
}

// ===== OLED =====
void mostrarMensagem(String msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println(msg);
  display.display();
}

// ===== CONTROLE DO PORTÃO =====
void abrirPortao() {
  servoPortao.write(anguloAberto);
  portaoAberto = true;
  tempoAbertura = millis();

  digitalWrite(LED_VERDE, HIGH);
  digitalWrite(LED_VERMELHO, LOW);
  buzzerBeep(1000, 300);
  mostrarMensagem("Bem-vindo!");

  publicarStatus();
}

void fecharPortao() {
  servoPortao.write(anguloFechado);
  portaoAberto = false;

  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, HIGH);
  buzzerBeep(600, 200);
  mostrarMensagem("Volte sempre!");

  publicarStatus();
}

void togglePortao() {
  if (portaoAberto) fecharPortao();
  else abrirPortao();
}

// ===== MQTT =====
void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando MQTT...");
    if (client.connect("ESP32-Portao")) {
      Serial.println("Conectado!");
      client.subscribe("portao/controle");
    } else {
      Serial.print("Falhou, rc=");
      Serial.println(client.state());
      delay(3000);
    }
  }
}

void callback(char* topic, byte* message, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) msg += (char)message[i];

  if (msg == "abrir") abrirPortao();
  if (msg == "fechar") fecharPortao();
  if (msg == "toggle") togglePortao();
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  servoPortao.attach(SERVO_PIN);

  // Inicializa OLED nos pinos 5(SDA) e 17(SCL)
  I2C_PORT.begin(5, 17);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Erro OLED");
    for(;;);
  }
  mostrarMensagem("Sistema Portao");

  // Teste do movimento completo do servo
  servoPortao.write(anguloFechado);
  delay(1000);
  servoPortao.write(anguloAberto);
  delay(1000);
  servoPortao.write(anguloFechado);
  delay(500);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" WiFi Conectado!");

  configTime(0, 0, "pool.ntp.org");

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, HIGH);
  publicarStatus();
}

// ===== LOOP =====
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  bool movimento = digitalRead(PIR_PIN);
  bool botao = !digitalRead(BUTTON_PIN);

  // --- Botão manual ---
  if (botao) {
    togglePortao();
    delay(300);
  }

  // --- Movimento pelo PIR ---
  if (movimento && !portaoAberto) {
    abrirPortao();
    delay(200);
  }

  // --- Ultrassônico ---
  long distancia = medirDistancia();
  if (distancia > 0 && distancia < distanciaLimite && !portaoAberto) {
    Serial.println("Objeto detectado a " + String(distancia) + " cm - Abrindo portão!");
    abrirPortao();
    delay(200);
  }

  // --- Fechamento automático ---
  if (portaoAberto && (millis() - tempoAbertura > tempoFechar)) {
    fecharPortao();
  }
}
