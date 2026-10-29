#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

const char *ssid = "";
const char *password = "";

// Облікові дані для доступу до захищеної вебпанелі (Basic Auth)
const char *www_user = "";
const char *www_pass = "";

ESP8266WebServer server(80);
WiFiUDP ntpUDP;
// NTP-клієнт: сервер часу, зміщення у секундах (наприклад, UTC+2 = 2 * 3600), інтервал оновлення
NTPClient timeClient(ntpUDP, "pool.ntp.org", 2 * 3600, 60000);

enum States
{
  CONNECTING,
  RUNNING,
  NOT_CONNECTED
};

States cur_state = CONNECTING;

unsigned long last_time_wifi = 0;
unsigned long last_time_timeout = 0;
unsigned long last_time_sec = 0;

void setup()
{
  Serial.begin(115200);
  delay(2000);

  // Налаштування Wi-Fi у режимі станції
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  last_time_timeout = millis();

  // Запуск NTP-клієнта
  timeClient.begin();

  // Налаштування головної сторінки із ЗАХИСТОМ (Basic Auth)
  server.on("/", []()
            {
    // Перевірка введених даних автентифікації
    if (!server.authenticate(www_user, www_pass))
    {
      // Якщо дані невірні або відсутні, браузер покаже вікно введення логіна/пароля
      return server.requestAuthentication();
    }
    
    // Якщо доступ дозволено — генеруємо захищену HTML-сторінку
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                  "<title>Automation Hub</title></head><body>"
                  "<h2>Local Automation Hub (Protected)</h2>"
                  "<p>Status: <span style='color:green;'>Online</span></p>"
                  "<p>Current NTP Time: <b>" + timeClient.getFormattedTime() + "</b></p>"
                  "</body></html>";
                  
    server.send(200, "text/html", html); });

  server.begin();

  // Запуск mDNS (доступ за адресою http://esp8266.local)
  if (MDNS.begin("esp8266"))
  {
    Serial.println("MDNS responder started");
  }
  MDNS.addService("http", "tcp", 80);
}

void loop()
{
  unsigned long t = millis();

  switch (cur_state)
  {
  case CONNECTING:
    // Неблокуюча перевірка підключення до Wi-Fi
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("\nWiFi connected successfully!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      cur_state = RUNNING;
    }
    else
    {
      // Індикація підключення крапками раз на 500 мс
      if (t - last_time_wifi >= 500)
      {
        last_time_wifi = t;
        Serial.print(".");
      }

      // Таймаут підключення (30 секунд)
      if (t - last_time_timeout >= 30000)
      {
        Serial.println("\nConnection TIMEOUT! Switching to NOT_CONNECTED.");
        cur_state = NOT_CONNECTED;
        last_time_timeout = t;
      }
    }
    break;

  case NOT_CONNECTED:
    // Очікування 5 секунд перед перезавантаженням при збої мережі
    if (t - last_time_timeout >= 7000)
    {
      Serial.println("Restarting ESP8266...");
      ESP.restart();
    }
    break;

  case RUNNING:
    // Обробка клієнтів вебсервера
    server.handleClient();

    // Моніторинг втрати зв'язку з Wi-Fi під час роботи
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("WiFi connection lost! Reconnecting...");
      WiFi.reconnect();
      cur_state = CONNECTING;
      last_time_timeout = t;
      break;
    }

    // Фонова перебіжка часу та виконання задач раз на 1 секунду
    if (t - last_time_sec >= 1000)
    {
      last_time_sec = t;
      timeClient.update(); // Оновлення часу з NTP-сервера

      int currentHour = timeClient.getHours();
      int currentMinute = timeClient.getMinutes();
      int currentSecond = timeClient.getSeconds();

      // Вивід поточного часу в Serial Monitor
      Serial.print("Time: ");
      Serial.println(timeClient.getFormattedTime());

      // --- ПРИКЛАД NTP-ТАЙМЕРА (АВТОМАТИЗАЦІЯ ЗА РОЗКЛАДОМ) ---
      // Наприклад, спрацьовує щодня рівно о 08:00:00 (увімкнення реле, обігрівача тощо)
      if (currentHour == 8 && currentMinute == 0 && currentSecond == 0)
      {
        Serial.println("[AUTOMATION] Triggered scheduled event: Morning routine!");
        // Тут прописується логіка керування реле або GPIO (наприклад, digitalWrite(RelayPin, HIGH);)
      }

      // Приклад вечірньої події о 22:30:00
      if (currentHour == 19 && currentMinute == 56)
      {
        Serial.println("[AUTOMATION] Triggered scheduled event: Night mode!");
      }
    }
    break;

  default:
    break;
  }
}